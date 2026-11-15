#include <stdlib.h>
#include <queue>
#include <iostream>
#include <string>
#include <stdio.h>
#include <tchar.h>
#include <assert.h>
#include <omp.h>
#include <algorithm>
#include <map>
#include "nanoflann.h"
#include "Clock.h"

using namespace nanoflann;

#define SQR(x) ((x)*(x))
#define DIST2(X,Y) ( SQR(X.x-Y.x) + SQR(X.y-Y.y) + SQR(X.z-Y.z) )
#define MAXMIN(x,lower,upper) ceil(max(min((double)(x),(double)(upper)),(double)(lower)))
#define MINUS(a,b,out) {(out).x=(a).x-(b).x; (out).y=(a).y-(b).y;(out).z=(a).z-(b).z;}
typedef unsigned int uint;
struct PointCloud;
typedef KDTreeSingleIndexAdaptor<L2_Simple_Adaptor<float, PointCloud> ,	PointCloud,	3> NanoKDTree;

Clock MS_clk;

using namespace std;

struct Mode { Point3f mode; float weight; };

// This is an exampleof a custom data set class
class PointCloud
{	
public:
	std::vector<Point3f>  pts;

	// Must return the number of data points
	inline size_t kdtree_get_point_count() const { return pts.size(); }

	// Returns the distance between the vector "p1[0:size-1]" and the data point with index "idx_p2" stored in the class:
	inline float kdtree_distance(const float *p1, const size_t idx_p2, size_t size) const
	{
		const float d0=p1[0]-pts[idx_p2].x;
		const float d1=p1[1]-pts[idx_p2].y;
		const float d2=p1[2]-pts[idx_p2].z;
		return (d0*d0+d1*d1+d2*d2);
	}

	// Returns the dim'th component of the idx'th point in the class:
	// Since this is inlined and the "dim" argument is typically an immediate value, the
	//  "if/else's" are actually solved at compile time.
	inline float kdtree_get_pt(const size_t idx, int dim) const
	{
		if (dim==0) return pts[idx].x;
		else if (dim==1) return pts[idx].y;
		else return pts[idx].z;
	}

	// Optional bounding-box computation: return false to default to a standard bbox computation loop.
	//   Return true if the BBOX was already computed by the class and returned in "bb" so it can be avoided to redo it again.
	//   Look at bb.size() to find out the expected dimensionality (e.g. 2 or 3 for point clouds)
	template <class BBOX>
	bool kdtree_get_bbox(BBOX &bb) const { return false; }
};

//outList are the returned modes
// usage: meanShift ( &vector<xyz>[0] or &mat.data[0], bandwidth, vec of modes)
/*
std::vector<std::pair<size_t,float> > ret_matches;
ret_matches.reserve(cloud.size());

PointCloud pointCloud; pointCloud.pts=cloud;


NanoKDTree kdtree(3, pointCloud, KDTreeSingleIndexAdaptorParams(10) );
kdtree.buildIndex();

// Perform meanshift clustering on hand pixels.
vector<Mode> outList;
meanShift(pointCloud, kdtree, 80, outList);
*/
void meanShift(PointCloud &cloud, NanoKDTree &kdtree, float bandwidth, vector<Mode> &outList)
{
	if (cloud.pts.size()==0) return;

	std::vector<Point3f>  &inList = cloud.pts;
	std::vector<std::pair<size_t,float> > ret_matches;
	ret_matches.reserve(inList.size());

	Clock clk;
	clk.tic();

	std::vector<bool> visitedList(inList.size(),false);
	std::vector<float> out_dist_sqr(inList.size());

	float stopThresh = SQR( 1e-3 * bandwidth );
	float bwSq = SQR( bandwidth );
	float inClusterDelta = SQR( bandwidth / 2.0f );
	int maxItrs = 4;
	int N = inList.size();

	int countSeeds=0;
	int countSearches=0;
	outList.clear();

	//printf("Setup time:%f  ",clk.toc());
	for (int n = 0; n < N; n++)
		if (!visitedList[n])
		{
			countSeeds++;
			visitedList[n]=true;
			//int n = floor(rand()/(float)RAND_MAX*(float)N);
			int itrCnt = 0;
			float delta = 1e20;
			Point3f mu = inList[n];
			Point3f muOld;
			float sz;
			while (/*itrCnt < maxItrs &&*/ delta > stopThresh)
			{
				countSearches++;
				muOld = mu;
				mu.x=mu.y=mu.z=0;
				nanoflann::SearchParams params;
				params.sorted = false;

				// find nearest neighbours

				//clk.tic();
				kdtree.radiusSearch(&muOld.x, bwSq, ret_matches, params);
				//printf("Search: %f  #res:%i\n",clk.toc(),ret_matches.size());
				// go over all nn and mark them as visited and take their average 
				sz = ret_matches.size();
				for (int nn=0; nn < sz; nn++)
				{
					int ind=ret_matches[nn].first;
					visitedList[ind]=true;
					Point3f val = cloud.pts[ind];
					mu.x+=val.x;mu.y+=val.y;mu.z+=val.z;					
				}
				mu.x/=sz;mu.y/=sz;mu.z/=sz;

				// find distance between old average and new one
				delta = DIST2(mu, muOld);
				itrCnt++;
			}

			//See if mu is within b/2 of another cluster
			int mergeWith = outList.size();
			float currentMin = inClusterDelta;
			for (int c = 0; c < outList.size(); c++)
				if (DIST2(mu, outList[c].mode) < currentMin) // then this point must be merged
				{mergeWith = c; break; /* currentMin = DIST2(mu, outList[c].mode);*/} //POTENTIAL BUG : more than one outList mode might be within range so we need to pick the one who is the closest i.e iterate over the whole of outlist

				if (mergeWith == outList.size()) // then we arent close to anybody
				{
					Mode m;
					m.mode = mu;
					m.weight = sz;
					outList.push_back(m); // so we add a new cluster
				} else // we are close to someone else so we just join it
				{
					float w = outList[mergeWith].weight;
					/*outList[mergeWith].mode.x = ( w*outList[mergeWith].mode.x + mu.x ) / (w + 1);
					outList[mergeWith].mode.y = ( w*outList[mergeWith].mode.y + mu.y ) / (w + 1);
					outList[mergeWith].mode.z = ( w*outList[mergeWith].mode.z + mu.z ) / (w + 1);
					*/
					outList[mergeWith].mode.x = ( outList[mergeWith].mode.x + mu.x ) / (2);
					outList[mergeWith].mode.y = ( outList[mergeWith].mode.y + mu.y ) / (2);
					outList[mergeWith].mode.z = ( outList[mergeWith].mode.z + mu.z ) / (2);
					outList[mergeWith].weight+=sz;
				}
		}
		//printf("Seeds:%i  Searches:%i \n",countSeeds, countSearches);
}

