import Foundation
import Metal
import CryptoKit

// Tree traversal, four exact normalized integer probes and every branch run on
// Metal. Optional reached-leaf output is enabled only in validation, not timing.
let kernelSource = #"""
#include <metal_stdlib>
using namespace metal;
kernel void depth_tree(device const ushort *image [[buffer(0)]],
                       device const short *nodes [[buffer(1)]],
                       device const uchar *leaves [[buffer(2)]],
                       device uchar *labels [[buffer(3)]],
                       device ushort *reached [[buffer(4)]],
                       constant uint &levels [[buffer(5)]],
                       constant uint &trace [[buffer(6)]],
                       uint i [[thread_position_in_grid]]) {
    if(i>=16384) return;
    int z=int(image[i]);
    if(z==0) {
        labels[i]=7;
        if(trace) reached[i]=65535;
        return;
    }
    int row=int(i/128),col=int(i%128);
    uint node=0,path=0;
    for(uint level=0;level<levels;++level) {
        uint base=node*6;
        int r1=(row*z+int(nodes[base]))/z;
        int c1=(col*z+int(nodes[base+1]))/z;
        int r2=(row*z+int(nodes[base+2]))/z;
        int c2=(col*z+int(nodes[base+3]))/z;
        int a=(r1<0||r1>=128||c1<0||c1>=128)?0:int(image[r1*128+c1]);
        int b=(r2<0||r2>=128||c2<0||c2>=128)?0:int(image[r2*128+c2]);
        uint flags=ushort(nodes[base+5]);
        bool right;
        if(a==0&&b==0) right=bool(flags&1);
        else if(a==0) right=bool(flags&2);
        else if(b==0) right=bool(flags&4);
        else if(flags&8) right=false;
        else if(flags&16) right=true;
        else right=(a-b)>int(nodes[base+4]);
        path=path*2+uint(right);
        node=node*2+1+uint(right);
    }
    labels[i]=leaves[path];
    if(trace) reached[i]=ushort(node);
}
"""#

struct Failure: Error, CustomStringConvertible {
    let description:String
    init(_ text:String){description=text}
}
func digest(_ data:Data)->String { SHA256.hash(data:data).map{String(format:"%02x",$0)}.joined() }
func now()->UInt64 { DispatchTime.now().uptimeNanoseconds }
func ms(_ a:UInt64,_ b:UInt64)->Double {Double(b-a)/1_000_000}
func stats(_ values:[Double])->[String:Any] {
    let a=values.sorted(),n=a.count
    if n == 0 { return ["count":0] }
    return ["count":n,"min":a.first!,"max":a.last!,"mean":a.reduce(0,+)/Double(n),
            "median":n%2==0 ? (a[n/2-1]+a[n/2])/2:a[n/2],"p95":a[Int(ceil(Double(n)*0.95))-1]]
}
struct Sample:Codable {
    let iteration:Int,frame_index:Int
    let gpu_ms:Double,host_input_to_labels_ms:Double,input_copy_ms:Double,output_copy_ms:Double
    let gpu_start_s:Double,gpu_end_s:Double
}

struct Options {
    var input="",nodes="",leaves="",output=""
    var warmup=30,iterations=600,levels=10
    init()throws {
        var args=Array(CommandLine.arguments.dropFirst())
        while !args.isEmpty {
            let key=args.removeFirst()
            guard !args.isEmpty else{throw Failure("Missing option value for \(key)")}
            let value=args.removeFirst()
            switch key {
            case "--input":input=value
            case "--nodes":nodes=value
            case "--leaves":leaves=value
            case "--output-dir":output=value
            case "--warmup":guard let v=Int(value),(0...1000).contains(v)else{throw Failure("Bad warmup")};warmup=v
            case "--iterations":guard let v=Int(value),(1...10000).contains(v)else{throw Failure("Bad iterations")};iterations=v
            case "--levels":guard let v=Int(value),(1...10).contains(v)else{throw Failure("Bad levels")};levels=v
            default:throw Failure("Unknown option \(key)")
            }
        }
        guard !input.isEmpty && !nodes.isEmpty && !leaves.isEmpty && !output.isEmpty else {
            throw Failure("Usage: tree_metal --input FRAMES.u16 --nodes nodes.bin --leaves leaves.bin --output-dir NEWDIR [--levels10 --warmup30 --iterations600]")
        }
    }
}

final class Runner {
    let device:MTLDevice,queue:MTLCommandQueue,pipeline:MTLComputePipelineState
    let image:MTLBuffer,nodes:MTLBuffer,leaves:MTLBuffer,labels:MTLBuffer,reached:MTLBuffer
    let sources:[Data],options:Options
    var hostLabels=[UInt8](repeating:0,count:16384)
    let deadline=now()+120_000_000_000
    init(_ options:Options,_ input:Data,_ nodeData:Data,_ leafData:Data)throws {
        self.options=options
        guard let d=MTLCreateSystemDefaultDevice(),let q=d.makeCommandQueue() else {throw Failure("Metal GPU unavailable; no CPU fallback")}
        device=d;queue=q
        let compile=MTLCompileOptions()
        if #available(macOS 15.0, *) { compile.mathMode = .safe } else { compile.fastMathEnabled = false }
        let library=try d.makeLibrary(source:kernelSource,options:compile)
        guard let f=library.makeFunction(name:"depth_tree")else{throw Failure("Metal kernel missing")}
        pipeline=try d.makeComputePipelineState(function:f)
        func buffer(_ data:Data)->MTLBuffer? {data.withUnsafeBytes{p in d.makeBuffer(bytes:p.baseAddress!,length:data.count,options:[.storageModeShared,.hazardTrackingModeTracked])}}
        guard let ib=d.makeBuffer(length:32768,options:[.storageModeShared,.hazardTrackingModeTracked]),
              let nb=buffer(nodeData),let lb=buffer(leafData),
              let ob=d.makeBuffer(length:16384,options:[.storageModeShared,.hazardTrackingModeTracked]),
              let rb=d.makeBuffer(length:32768,options:[.storageModeShared,.hazardTrackingModeTracked]) else {throw Failure("Metal allocation failed")}
        image=ib;nodes=nb;leaves=lb;labels=ob;reached=rb
        sources=(0..<input.count/32768).map{input.subdata(in:$0*32768..<($0+1)*32768)}
    }
    func execute(_ frame:Int,_ iteration:Int,trace:Bool=false)throws->Sample {
        guard now()<deadline else {throw Failure("Bounded Metal benchmark exceeded120s")}
        let start=now()
        sources[frame].withUnsafeBytes{p in image.contents().copyMemory(from:p.baseAddress!,byteCount:32768)}
        let copied=now()
        guard let cmd=queue.makeCommandBuffer(),let enc=cmd.makeComputeCommandEncoder()else{throw Failure("Command allocation failed")}
        cmd.label="Depth tree single frame \(frame)";enc.setComputePipelineState(pipeline)
        for (i,b) in [image,nodes,leaves,labels,reached].enumerated(){enc.setBuffer(b,offset:0,index:i)}
        var levels=UInt32(options.levels),traceEnabled:UInt32=trace ? 1:0
        enc.setBytes(&levels,length:4,index:5);enc.setBytes(&traceEnabled,length:4,index:6)
        let width=min(128,pipeline.maxTotalThreadsPerThreadgroup)
        enc.dispatchThreadgroups(MTLSize(width:(16384+width-1)/width,height:1,depth:1),threadsPerThreadgroup:MTLSize(width:width,height:1,depth:1))
        enc.endEncoding();cmd.commit();cmd.waitUntilCompleted()
        let completed=now()
        guard cmd.status == .completed else{throw Failure("Metal command failed: \(String(describing:cmd.error))")}
        hostLabels.withUnsafeMutableBytes{p in p.baseAddress!.copyMemory(from:labels.contents(),byteCount:16384)}
        let ready=now(),a=cmd.gpuStartTime,b=cmd.gpuEndTime
        guard a.isFinite && b.isFinite && a>0 && b>a else{throw Failure("Actual GPU timestamps unavailable")}
        return Sample(iteration:iteration,frame_index:frame,gpu_ms:(b-a)*1000,host_input_to_labels_ms:ms(start,ready),input_copy_ms:ms(start,copied),output_copy_ms:ms(completed,ready),gpu_start_s:a,gpu_end_s:b)
    }
}

func run()throws {
    let o=try Options(),input=try Data(contentsOf:URL(fileURLWithPath:o.input)),nodeData=try Data(contentsOf:URL(fileURLWithPath:o.nodes)),leafData=try Data(contentsOf:URL(fileURLWithPath:o.leaves))
    guard input.count>0 && input.count%32768==0 && input.count/32768<=256 else{throw Failure("Input needs1..256 concatenated128x128u16 frames")}
    guard nodeData.count==12*((1<<o.levels)-1) && leafData.count==(1<<o.levels)else{throw Failure("Node/leaf sizes do not match levels")}
    for i in 0..<(1<<o.levels)-1 {
        let flags=UInt16(nodeData[i*12+10])|UInt16(nodeData[i*12+11])<<8
        guard flags&32 != 0 && flags&0xffc0 == 0 && flags&24 != 24 else{throw Failure("Invalid packed node\(i)")}
    }
    let out=URL(fileURLWithPath:o.output).standardizedFileURL
    if FileManager.default.fileExists(atPath:out.path) {
        guard try FileManager.default.contentsOfDirectory(atPath:out.path).isEmpty else{throw Failure("Output directory must be empty")}
    }else{try FileManager.default.createDirectory(at:out,withIntermediateDirectories:true)}
    let r=try Runner(o,input,nodeData,leafData)
    var outputs:[Data]=[],frames:[[String:Any]]=[]
    for i in 0..<r.sources.count {
        _=try r.execute(i,-1,trace:true)
        let labels=Data(r.hostLabels),trace=Data(bytes:r.reached.contents(),count:32768)
        outputs.append(labels)
        let name=String(format:"frame_%03d.labels.bin",i),tn=String(format:"frame_%03d.leafids.bin",i)
        try labels.write(to:out.appendingPathComponent(name));try trace.write(to:out.appendingPathComponent(tn))
        frames.append(["frame_index":i,"input_sha256":digest(r.sources[i]),"labels_file":name,"labels_sha256":digest(labels),"leafids_file":tn,"leafids_sha256":digest(trace)])
    }
    for i in 0..<o.warmup{_=try r.execute(i%r.sources.count,-1)}
    var samples:[Sample]=[]
    for i in 0..<o.iterations {
        let frame=i%r.sources.count,s=try autoreleasepool{try r.execute(frame,i)}
        guard Data(r.hostLabels)==outputs[frame]else{throw Failure("Timed GPU output differs forframe\(frame)")}
        samples.append(s)
    }
    let encoder=JSONEncoder(),sampleJSON=try JSONSerialization.jsonObject(with:encoder.encode(samples))
    let perFrame=(0..<r.sources.count).map{i -> [String:Any] in let a=samples.filter{$0.frame_index==i};return ["frame_index":i,"gpu_ms":stats(a.map(\.gpu_ms)),"host_input_to_labels_ms":stats(a.map(\.host_input_to_labels_ms))]}
    let report:[String:Any]=["implementation":"actualMetalGPUintegerdepthtree","cpu_fallback":false,"levels":o.levels,"width":128,"height":128,"background_label":7,"unknown_label":255,"created_utc":ISO8601DateFormatter().string(from:Date()),"device":["name":r.device.name,"registry_id":String(r.device.registryID),"unified_memory":r.device.hasUnifiedMemory],"host_os":ProcessInfo.processInfo.operatingSystemVersionString,"thermal_state":ProcessInfo.processInfo.thermalState.rawValue,"input_sha256":digest(input),"nodes_sha256":digest(nodeData),"leaves_sha256":digest(leafData),"kernel_source_sha256":digest(Data(kernelSource.utf8)),"executable_sha256":digest(try Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[0]))),"warmup_iterations_excluded":o.warmup,"benchmark_iterations":o.iterations,"one_frame_in_flight":true,"frames":frames,"per_frame":perFrame,"gpu_ms":stats(samples.map(\.gpu_ms)),"host_input_to_labels_ms":stats(samples.map(\.host_input_to_labels_ms)),"samples":sampleJSON,"timed_labels_equal_output_pass":true,"timing_scope":["gpu_ms":"MTLCommandBuffer.gpuEndTime-gpuStartTime after completion; single tree-classification kernel, no trace output, oneframepercommandbuffer","host_input_to_labels_ms":"CPUwalltime inputcopy32768B into preallocatedsharedGPUbuffer,encode/submit/wait,copy16384classbytes topreallocatedCPUstorage","trace":"Reachedheapleafids producedonlyduringuntimed validation, nottimedkernelwrites orhostcopies","excluded":"FileI/O,modelupload,librarycompilation,allocation,warmups,hashing,validationcomparisons,rendering,cameracapture","input":"IdenticalpreprocessedCPUdepthbytes;modelloadedonce;noCPUtreecomputationorGPUfallback"]]
    try JSONSerialization.data(withJSONObject:report,options:[.prettyPrinted,.sortedKeys,.withoutEscapingSlashes]).write(to:out.appendingPathComponent("benchmark.json"))
    print("Metal device: \(r.device.name); \(r.sources.count)frames; \(samples.count)measuredsingleframeiterations")
    print("GPUmedianms: \(stats(samples.map(\.gpu_ms))["median"]!); hostmedianms: \(stats(samples.map(\.host_input_to_labels_ms))["median"]!)")
}
do{try run()}catch{fputs("tree_metal: \(error)\n",stderr);exit(1)}
