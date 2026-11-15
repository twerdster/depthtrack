#pragma once
// Portable packed-model CPU inference. See ../NOTICE.md for model provenance.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace depthtrack {
class PackedTree {
    struct Node { int row1,col1,row2,col2,cut;unsigned flags; };
    std::vector<Node> nodes_;
    std::vector<std::uint8_t> leaves_;
    unsigned levels_;
    static unsigned u16(const std::uint8_t* p) {return unsigned(p[0])|(unsigned(p[1])<<8);}
    static int s16(const std::uint8_t* p) {unsigned v=u16(p);return v>=32768?int(v)-65536:int(v);}
public:
    static constexpr int width=128,height=128,pixels=width*height;
    static std::vector<std::uint8_t> read(const std::filesystem::path& path) {
        std::ifstream f(path,std::ios::binary|std::ios::ate);
        if(!f)throw std::runtime_error("Cannot open "+path.string());
        auto size=f.tellg();if(size<0)throw std::runtime_error("Invalid file size");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));f.seekg(0);
        if(!bytes.empty()&&!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Short read");
        return bytes;
    }
    PackedTree(const std::vector<std::uint8_t>& nodes,const std::vector<std::uint8_t>& leaves,unsigned levels=10):leaves_(leaves),levels_(levels) {
        if(levels<1||levels>10||nodes.size()!=((1u<<levels)-1)*12||leaves.size()!=(1u<<levels))throw std::invalid_argument("Incorrect model dimensions");
        for(std::size_t base=0;base<nodes.size();base+=12) {
            const auto* p=nodes.data()+base;unsigned flags=u16(p+10);
            if(!(flags&32)||(flags&0xffc0)||(flags&24)==24)throw std::invalid_argument("Invalid node flags");
            nodes_.push_back({s16(p),s16(p+2),s16(p+4),s16(p+6),s16(p+8),flags});
        }
    }
    // Input/output buffers remain caller-owned. No allocations occur here.
    // Coordinates truncate after the complete base+offset/z sum; OOB is zero.
    void predict(const std::uint16_t* image,std::uint8_t* labels,std::uint16_t* heap_trace=nullptr) const {
        if(!image||!labels)throw std::invalid_argument("Null image/output buffer");
        for(int i=0;i<pixels;++i) {
            int z=image[i];if(!z){labels[i]=7;if(heap_trace)heap_trace[i]=65535;continue;}
            int y=i/width,x=i%width;unsigned node=0;
            auto sample=[&](int dr,int dc) {int r=(y*z+dr)/z,c=(x*z+dc)/z;return r>=0&&r<height&&c>=0&&c<width?int(image[r*width+c]):0;};
            for(unsigned level=0;level<levels_;++level) {
                const auto& n=nodes_[node];int a=sample(n.row1,n.col1),b=sample(n.row2,n.col2);bool right;
                if(!a&&!b)right=n.flags&1;else if(!a)right=n.flags&2;else if(!b)right=n.flags&4;
                else if(n.flags&8)right=false;else if(n.flags&16)right=true;else right=a-b>n.cut;
                node=2*node+1+unsigned(right);
            }
            labels[i]=leaves_[node-((1u<<levels_)-1)];if(heap_trace)heap_trace[i]=std::uint16_t(node);
        }
    }
};
}
