#include "depth_tree.hpp"
#include <iostream>
int main(int argc,char**argv){try{
    if(argc!=5&&argc!=6)throw std::invalid_argument("Usage: tree_cpu nodes.bin leaves.bin depth.u16 labels.u8 [heapids.u16]");
    auto nodes=depthtrack::PackedTree::read(argv[1]),leaves=depthtrack::PackedTree::read(argv[2]),input=depthtrack::PackedTree::read(argv[3]);
    if(input.size()!=32768)throw std::invalid_argument("Input must be exactly128x128 uint16 little-endian millimetres");
    depthtrack::PackedTree tree(nodes,leaves);
    std::vector<std::uint16_t> depth(16384),heaps(16384);std::vector<std::uint8_t> labels(16384);
    for(int i=0;i<16384;++i)depth[i]=unsigned(input[2*i])|(unsigned(input[2*i+1])<<8);
    tree.predict(depth.data(),labels.data(),argc==6?heaps.data():nullptr);
    std::ofstream output(argv[4],std::ios::binary);output.write(reinterpret_cast<char*>(labels.data()),labels.size());
    if(!output)throw std::runtime_error("Label output failed");
    if(argc==6){std::ofstream trace(argv[5],std::ios::binary);for(auto v:heaps){trace.put(char(v&255));trace.put(char(v>>8));}if(!trace)throw std::runtime_error("Trace output failed");}
    std::cout<<"Wrote16384 labels; background7 unknown255\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
