`timescale 1ns/1ps
module tb_thesis_core;
    parameter WIDTH=16,HEIGHT=12,ADDR_WIDTH=8,CONTEXTS=4;
    localparam PIXELS=WIDTH*HEIGHT;
    reg clk=0;always #10 clk=~clk;
    reg reset=1,frame_we=0,model_we=0,model_is_leaf=0,start=0,model_clear=0;
    reg [ADDR_WIDTH-1:0] frame_addr=0,label_addr=0;
    reg [15:0] frame_data=0;
    reg [9:0] model_addr=0;
    reg [95:0] model_data=0;
    reg [3:0] levels=10;
    wire busy,done,error;
    wire [31:0] cycles,valid_pixels;
    wire [10:0] model_node_count,model_leaf_count;
    wire [7:0] label_data;
    thesis_tree_core #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.ADDR_WIDTH(ADDR_WIDTH),.CONTEXTS(CONTEXTS)) dut(.*);
    reg [15:0] image[0:PIXELS-1];
    reg [95:0] nodes[0:1022];
    reg [7:0] leaves[0:1023],expected[0:PIXELS-1];
    string input_file,node_file,leaf_file,expected_file,output_file,leafid_file;
    reg [15:0] expected_leafid[0:PIXELS-1];
    reg [PIXELS-1:0] leaf_seen=0;
    reg check_leafid=0;
    integer leaf_paths_checked=0,leaf_pixel;
    integer i,level_arg,expected_valid,watchdog,active_max,inflight_max,active,inflight,j,fd,cycle_count;
    always @(posedge clk) if(!reset && check_leafid && dut.leaf_read) begin
        leaf_pixel=dut.ctx_pixel[dut.memory_tag];
        if(leaf_seen[leaf_pixel])$fatal(1,"duplicate leaf pixel %0d",leaf_pixel);
        if(image[leaf_pixel]==0)$fatal(1,"zero centre traversed");
        if(expected_leafid[leaf_pixel]!==((1<<level_arg)-1+dut.next_path))
            $fatal(1,"full leaf path mismatch pixel %0d got %0d expected %0d",leaf_pixel,(1<<level_arg)-1+dut.next_path,expected_leafid[leaf_pixel]);
        leaf_seen[leaf_pixel]=1;leaf_paths_checked=leaf_paths_checked+1;
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        if(!$value$plusargs("IMAGE=%s",input_file))$fatal(1,"IMAGE missing");
        if(!$value$plusargs("NODES=%s",node_file))$fatal(1,"NODES missing");
        if(!$value$plusargs("LEAVES=%s",leaf_file))$fatal(1,"LEAVES missing");
        if(!$value$plusargs("EXPECTED=%s",expected_file))$fatal(1,"EXPECTED missing");
        if(!$value$plusargs("OUTPUT=%s",output_file))$fatal(1,"OUTPUT missing");
        if(!$value$plusargs("LEVELS=%d",level_arg))level_arg=10;
        $readmemh(input_file,image);$readmemh(node_file,nodes);$readmemh(leaf_file,leaves);$readmemh(expected_file,expected);
        if($value$plusargs("LEAFIDS=%s",leafid_file))begin $readmemh(leafid_file,expected_leafid);check_leafid=1;end
        repeat(3)tick();@(negedge clk);reset=0;
        expected_valid=0;
        for(i=0;i<PIXELS;i=i+1)begin
            @(negedge clk);frame_we=1;frame_addr=i;frame_data=image[i];tick();
            if(image[i]!=0)expected_valid=expected_valid+1;
        end
        @(negedge clk);frame_we=0;
        for(i=0;i<1023;i=i+1)begin
            @(negedge clk);model_we=1;model_is_leaf=0;model_addr=i;model_data=nodes[i];tick();
        end
        for(i=0;i<1024;i=i+1)begin
            @(negedge clk);model_is_leaf=1;model_addr=i;model_data=leaves[i];tick();
        end
        @(negedge clk);model_we=0;
        if(model_node_count!=1023 || model_leaf_count!=1024)$fatal(1,"model prefix counters");
        start=1;levels=level_arg;tick();
        @(negedge clk);start=0;
        watchdog=0;active_max=0;inflight_max=0;cycle_count=0;
        while(!done)begin
            tick();watchdog=watchdog+1;cycle_count=cycle_count+1;
            if(busy && label_data!==0)$fatal(1,"busy label guard");
            active=0;inflight=0;
            for(j=0;j<CONTEXTS;j=j+1)begin
                if(dut.ctx_state[j]!=0)active=active+1;
                if(dut.ctx_state[j]==3)inflight=inflight+1;
            end
            if(active>active_max)active_max=active;
            if(inflight>inflight_max)inflight_max=inflight;
            if(watchdog>PIXELS*150+1000)$fatal(1,"core timeout");
        end
        if(error)$fatal(1,"unexpected core error");
        if(valid_pixels!==expected_valid)$fatal(1,"valid count %d expected %d",valid_pixels,expected_valid);
        if(check_leafid && leaf_paths_checked!=expected_valid)$fatal(1,"leaf count");
        if(cycles!==cycle_count)$fatal(1,"cycle count %d independently %d",cycles,cycle_count);
        fd=$fopen(output_file,"w");
        for(i=0;i<PIXELS;i=i+1)begin
            @(negedge clk);label_addr=i;tick();
            if(label_data!==expected[i])$fatal(1,"label mismatch pixel %d got %d expected %d",i,label_data,expected[i]);
            $fwrite(fd,"%02x\n",label_data);
        end
        $fclose(fd);
        // Active writes and an extra start must not alter the completed model/image.
        // A fresh write invalidates old results immediately at the next edge.
        @(negedge clk);frame_we=1;frame_addr=0;frame_data=image[0];tick();
        if(label_data!==0)$fatal(1,"frame-write result invalidation");
        $display("PASS pixels=%0d levels=%0d valid=%0d cycles=%0d active_max=%0d inflight_max=%0d leaf_paths_checked=%0d",PIXELS,level_arg,expected_valid,cycles,active_max,inflight_max,leaf_paths_checked);
        $finish;
    end
endmodule
