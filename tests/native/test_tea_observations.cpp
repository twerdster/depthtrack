#include "depthtrack/tea_forest.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

std::vector<unsigned char> read(const std::filesystem::path& path,std::size_t size){std::vector<unsigned char> bytes(size);std::ifstream in(path,std::ios::binary);if(!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size))||in.peek()!=EOF)throw std::runtime_error("Bad observation fixture size");return bytes;}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected portable fixture manifest");
    depthtrack::tea::Forest forest(argv[1]);auto root=std::filesystem::path(argv[1]).parent_path();
    auto raw=read(root/"observations.depth.u16",forest.pixels()*2);std::vector<std::uint16_t> depth(forest.pixels());for(std::size_t i=0;i<depth.size();++i)depth[i]=std::uint16_t(raw[2*i])|(std::uint16_t(raw[2*i+1])<<8);
    raw=read(root/"observations.probabilities.f32",forest.pixels()*forest.classes()*4);std::vector<float> probabilities(forest.pixels()*forest.classes());for(std::size_t i=0;i<probabilities.size();++i){std::uint32_t bits=0;for(unsigned k=0;k<4;++k)bits|=std::uint32_t(raw[4*i+k])<<(8*k);std::memcpy(&probabilities[i],&bits,4);}
    depthtrack::tea::ObservationOptions options;options.class_ids={0,1,2,3,4};options.probability_threshold=.25;options.minimum_support=3;
    auto got=forest.observations(depth.data(),probabilities.data(),options);require(got.size()==5,"Must retain all five class slots");
    const bool present[]={true,true,false,true,false};
    const double expected[5][3]={{-.4,-.8,400},{-.35,0,350},{0,0,0},{1.35,0,450},{0,0,0}};
    for(std::size_t i=0;i<5;++i){require(got[i].class_id==i,"Class identity changed");require(got[i].present==present[i],"Missing/present differs");if(present[i]){require(got[i].support==3,"Component support differs");for(unsigned k=0;k<3;++k)require(std::abs(got[i].xyz[k]-expected[i][k])<1e-10,"Weighted XY/mean Z projection differs");}}
    options.probability_threshold=.5;got=forest.observations(depth.data(),probabilities.data(),options);require(!got[3].present,"Threshold override must remove class3");
    options.probability_threshold=.25;options.minimum_support=1;got=forest.observations(depth.data(),probabilities.data(),options);require(got[2].present&&got[4].present,"Support override must retain small components");
    std::cout<<"Five-class connected-component observations match portable XYZ/null fixture\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
