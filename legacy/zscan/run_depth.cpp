// Offline restoration of the image-processing path in Aaron's ZScanV02 Main.cpp.
// Camera/GUI/serial/robot/gesture code is deliberately excluded.
#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>
#include <chrono>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <limits>
#include <vector>
#include "source-audit/forest_adapter.hpp"
#include "algorithm-audit/depth_geometry.hpp"
#include <algorithm>
namespace cv {
template<class T> Mat_<T> operator*(const MatCommaInitializer_<T>& value) {
 return static_cast<Mat_<T>>(value);
}
}
namespace recovered2014 {
#include "TrackedPoint.h"
}
using cv::Point3f;
constexpr int W=320,H=240;
constexpr float FX=226.7364280140742f,FY=226.82657940719744f;
constexpr float CX=164.0663064783831f,CY=119.08355191679115f;
Point3f ray(float u,float v,float scale=1) {
 return Point3f((u-CX/scale)/(FX/scale),(v-CY/scale)/(FY/scale),1);
}
void point(std::ostream& o,const Point3f&p){o<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
cv::Mat asRows(std::vector<Point3f>& p){return p.empty()?cv::Mat():cv::Mat(p).reshape(1);}
void tracks(std::ostream&o,recovered2014::Tracker&t) {
 o<<'[';
 for(int j=0;j<t.numTrackedPoints();++j){
  if(j)o<<',';auto&tr=t.at(j);
  o<<"{\"age\":"<<tr.age<<",\"unmatched\":"<<tr.unmatchedCnt<<",\"history\":[";
  for(size_t i=0;i<tr.size();++i){if(i)o<<',';point(o,tr[i]);}o<<"]}";
 }o<<']';
}
int main(int argc,char**argv){try{
 if(argc!=6)throw std::runtime_error("Usage: run_depth depth.u16 frames hand.tree tip.tree output_dir");
 int n=std::stoi(argv[2]);std::filesystem::create_directories(argv[5]);
 std::string out=argv[5];std::ifstream input(argv[1],std::ios::binary);
 if(!input)throw std::runtime_error("Cannot read depth input");
 ExactForest handModel(argv[3],60,80),tipModel(argv[4],H,W);
 recovered2014::Tracker hand,fingers[5];
 hand.addTrackingTrigger(new recovered2014::SphereTrigger(Point3f(0,-100,300),150));
 hand.setMatchDistance(100);hand.setUnmatchedDeath(4);hand.setHistoryLength(10);
 hand.setMaxTrackedPoints(1);hand.setTrackAll(false);
 for(auto&f:fingers){
  f.addTrackingTrigger(new recovered2014::SphereTrigger(Point3f(0,0,0),80));
  f.setMatchDistance(60);f.setUnmatchedDeath(3);f.setHistoryLength(30);
  f.setMaxTrackedPoints(1);f.setTrackAll(false);
 }
 auto rays=depth2014::raysFromK(W,H,cv::Matx33f(FX,0,CX,0,FY,CY,0,0,1));
 cv::Mat full(H,W,CV_16U),floating;
 std::ofstream labelsOut(out+"/tip_labels.u8",std::ios::binary),maskOut(out+"/hand_masks.u8",std::ios::binary);
 std::ofstream json(out+"/tracks.json");json<<std::setprecision(9)<<"{\"frames\":[";
 for(int frame=0;frame<n;++frame){
  auto start=std::chrono::steady_clock::now();
  input.read(reinterpret_cast<char*>(full.data),H*W*2);
  if(input.gcount()!=H*W*2)throw std::runtime_error("Short depth input");
  cv::Mat coarse,work;cv::resize(full,coarse,cv::Size(80,60),0,0,cv::INTER_NEAREST);
  cv::medianBlur(coarse,coarse,3);cv::medianBlur(full,work,3);
  auto*d=coarse.ptr<uint16_t>();
  for(int i=0;i<4800;++i)d[i]=(d[i]>=100&&d[i]<=600)?d[i]*4:0;
  auto hlabels=handModel.infer(d,20);
  std::vector<Point3f> cloud;
  for(int i=0;i<4800;++i){d[i]=(hlabels[i]==1)?d[i]/4:0;if(d[i])cloud.push_back(ray(i%80,i/80,4)*float(d[i]));}
  auto hmodes=depth2014::meanShiftModes(cloud,80);
  std::vector<Point3f> hobs;
  for(auto&m:hmodes)if(m.weight>120)hobs.push_back(m.mode);
  hand.update(asRows(hobs));
  std::vector<uint8_t> mask(H*W,0),labels(H*W,9);
  std::vector<Point3f> observations[5];float weights[5]={};int classCounts[9]={};
  bool handReady=false;Point3f handPos;
  if(hand.numTrackedPoints() && hand.at(0).age>=3 && !cloud.empty()){
   hand.at(0).updateWithKalman(3);handPos=hand.at(0).back();handReady=true;
   // Original nearest coarse hand pixel is the flood seed. Bounds guards are
   // wrapper restorations; they do not alter valid in-bounds operations.
   size_t nearest=0;float distance=std::numeric_limits<float>::infinity();
   for(size_t i=0;i<cloud.size();++i){auto delta=cloud[i]-handPos;float dd=delta.dot(delta);if(dd<distance){distance=dd;nearest=i;}}
   Point3f P=cloud[nearest];float px=FX*P.x/P.z+CX,py=FY*P.y/P.z+CY;
   auto handMask=depth2014::handMask(work.ptr<uint16_t>(),H,W,rays,py,px,P.z,30,250);
   mask=std::move(handMask.mask);
   cv::Mat masked=work.clone();for(int i=0;i<H*W;++i)if(!mask[i])masked.ptr<uint16_t>()[i]=0;
   labels=tipModel.infer(masked.ptr<uint16_t>(),21);
   std::vector<Point3f> parts[5];
   for(int i=0;i<H*W;++i){if(labels[i]<9)++classCounts[labels[i]];if(labels[i]<5&&masked.ptr<uint16_t>()[i])parts[labels[i]].push_back(ray(i%W,i/W)*float(masked.ptr<uint16_t>()[i]));}
   for(int f=0;f<5;++f){
    auto modes=depth2014::meanShiftModes(parts[f],20);
    // Main's >0 mode-index guard skipped index zero. >=0 restores the strongest
    // existing mode; all five fingers enabled instead of GUI default Joint=0.
    int best=-1;float bestWeight=-1;
    for(size_t j=0;j<modes.size();++j)if(modes[j].weight>bestWeight){best=j;bestWeight=modes[j].weight;}
    if(best>=0){observations[f].push_back(modes[best].mode);weights[f]=bestWeight;}
   }
  }
  for(int f=0;f<5;++f){
   if(handReady)static_cast<recovered2014::SphereTrigger*>(fingers[f].trackingTriggers[0])->setCtr(handPos,100);
   // Empty observations also age/remove stale tracks, unlike Main's guard.
   fingers[f].setCanInitiate(handReady);fingers[f].update(asRows(observations[f]));
   for(int j=0;j<fingers[f].numTrackedPoints();++j)if(fingers[f].at(j).age>=4)fingers[f].at(j).updateWithKalman(4);
  }
  labelsOut.write(reinterpret_cast<char*>(labels.data()),labels.size());maskOut.write(reinterpret_cast<char*>(mask.data()),mask.size());
  if(frame)json<<',';
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  json<<"{\"frame\":"<<frame+1<<",\"processing_ms\":"<<ms<<",\"coarse_hand_pixels\":"<<cloud.size()<<",\"hand_modes\":"<<hmodes.size()<<",\"hand_observations\":"<<hobs.size()<<",\"hand_ready\":"<<(handReady?"true":"false")<<",\"hand_tracks\":";tracks(json,hand);
  json<<",\"class_counts\":[";for(int c=0;c<9;++c){if(c)json<<',';json<<classCounts[c];}json<<"],\"fingers\":[";
  for(int f=0;f<5;++f){if(f)json<<',';json<<"{\"class\":"<<f<<",\"mode_weight\":"<<weights[f]<<",\"observations\":[";for(size_t j=0;j<observations[f].size();++j){if(j)json<<',';point(json,observations[f][j]);}json<<"],\"tracks\":";tracks(json,fingers[f]);json<<'}';}
  json<<"]}";json.flush();
  std::cerr<<"frame "<<frame+1<<" hand pixels "<<cloud.size()<<" accepted modes "<<hobs.size()<<" ready "<<handReady<<" classes";for(int f=0;f<5;++f)std::cerr<<' '<<classCounts[f];std::cerr<<" ms "<<ms<<'\n';
 }
 json<<"]}\n";
 if(!json||!labelsOut||!maskOut)throw std::runtime_error("Output write failed");
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
