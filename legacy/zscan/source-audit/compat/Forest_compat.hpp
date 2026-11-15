

#include <stdexcept>
#include <assert.h>
//#include <boost/thread.hpp>
#include <queue>
#include <fstream>
#include <queue>
#include <iostream>
#include <string>
#include <stdio.h>
#include <cassert>
#include <sstream>
#include <iomanip>
#include <omp.h>
#include <algorithm>

// ZSHand.h
// Written by Aaron Wetzler, 2013


#ifndef ENSEMBLE
#define ENSEMBLE

#define ZSHAND_API

namespace ZSHand
{
	/** 
	Some basic typedefs to help readability 
	*/
	typedef void*          HND;	
	typedef unsigned char  uint8;
	typedef unsigned short ushort;
	typedef unsigned int   uint;	
	typedef unsigned short Flags;
	typedef uint8*         LabelType;
	typedef float*         WeightType;
	typedef ushort*        DepthType;
	typedef uint*          HistType;
	typedef uint*          LeafType;

	/**
	Defines somes basic error responses that can be returned by the available functions
	*/
	enum Error
	{
		NO_ERROR,
		ENSEMBLE_NOT_LOADED,
		ENSEMBLE_ALREADY_LOADED,
		ZERO_DIMENSION,
		NO_DATA_FILES,
		FILE_NOT_OPEN,
		OUT_OF_MEMORY,
		OUTPUT_NOT_ENABLED,
		OUTPUT_NOT_AVAILABLE
	};

	/**
	Defines the outputs that can be enabled and will be allocated and computed by the ensemble. 	
	*/
	enum OutputFlags
	{
		NONE			= 0x00,			///*  No data is output*/
		LABEL			= 0x01,			///*  Hard classification is made my taking a max over the posterior */
		HISTOGRAM		= 0x02,			///*  Soft classification is available either as histogram counts*/
		WEIGHTS 		= 0x04,			///*  Weights indicating how well supported the result at a pixel are*/
		LEAF			= 0x08,			///*  The number of the leaf that each pixel arrived at. Can be useful for collecting*/
		///*  similar pixels for further processing*/
	};

	/**
	Defines a set of hand elements that the current data set (DataSet1) knows how to classify
	*/
	enum HandPart
	{
		THUMB,			
		INDEX,			
		MIDDLE,  		
		RING,    		
		PINKY,			
		PALM,			
		WRIST,			
	};

	class Ensemble
	{
	public:

		/**
		Loads an ensemble classifier into the heap and provides a handle.
		The classifier provides a mechanism for classifying a given pixel
		as belonging to a certain class. The classes are predefined and hardcoded
		as part of the training process and cannot be extended (although conceptually 
		they can be concatenated by simply considering two separate classes to be the
		same class).
		Removing the ensemble completey clears its memory including all allocated outputs.

		hnd - The ensemble is passed around and referenced by use of a handle. 
		directoryName - Name of directory where ensemble files are
		*/
		static ZSHAND_API Error LoadEnsemble(HND& hnd, char* directoryName);
		static ZSHAND_API Error RemoveEnsemble(HND& hnd);



		/**		
		Enabling an output which is not enabled allocates it memory and does nothing if the 
		output has already been allocated. Disabling an output removes the allocated memory.
		Currently there are 4 possible outputs and they can be combined: LABEL, HISTOGRAM,
		WEIGHT and LEAF. See OutputFlags for a description of each.

		hnd - The handle to the ensemble
		flags - one or more of the 4 output options. Can also be  NONE
		width, height - dimensions of the output images which must be made to match those of the input
		*/
		static ZSHAND_API Error EnableOutputs(HND hnd, Flags flags, int width, int height); 
		static ZSHAND_API Error DisableOutputs(HND hnd, Flags flags); 



		/**		
		Applying an ensemble means to classify an image. The level here refers to the number 
		of features to query per pixel in order to update the outputs.
		Any pixel with a depth value of 0 will be skipped and will be considered background.

		hnd - The handle to the ensemble
		img - The input depth image in ushort format where the values are in milimetres. 
		This image needs to already have had its background removed and should preferably 
		have been through a smoothing filter like a median or bilaterial filter. Images are 
		considered to be stored in columnstack i.e. the first frameHeight elements are from the 
		first column.
		*/
		static ZSHAND_API Error ApplyEnsemble(HND hnd, DepthType img, int level);



		/**
		A set of functions for retrieving the outputs previously enabled.
		hnd - The handle to the ensemble
		img - A refernce pointer to the output data which are also in columnstack.
		*/
		static ZSHAND_API Error GetLabels(HND hnd, LabelType &img);
		static ZSHAND_API Error GetHistograms(HND hnd, HistType &img);
		static ZSHAND_API Error GetWeights(HND hnd, WeightType &img);
		static ZSHAND_API Error GetLeaves(HND hnd, LeafType &img); 



		/**		
		Obtain the number of classes that this ensemble knows how to classify
		*/
		static ZSHAND_API Error GetNumClasses(HND hnd, int& numClasses);
	};
}


#endif ENSEMBLE

#ifndef DEFINES
#define DEFINES
#define BLOCKSIZE_64  ( 64 * 1024 * 1024 )
#define BLOCKSIZE_8  ( 8 * 1024 * 1024 )
#define BLOCKSIZE_1   ( 1 * 1024 * 1024 )

#define DEBUG_LEVEL 40 
#define DEBUG(level, ...) {if (level<=DEBUG_LEVEL) printf(__VA_ARGS__);}
#define MAXMIN(x,lower,upper) std::ceil(std::max(std::min((double)(x),(double)(upper)),(double)(lower)))

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

struct RFNode
{
	int F;					
	int x1;					
	int y1;
	int x2;
	int y2;
	float thr;
};

struct CompactLeaf
{
	uint maxVal;
	uchar label;
};

struct TreeHeader
{
	int depth;
	int numClasses;
	int totalNodes;
	int totalHists;
};

struct Tree
{
	char * treeAlloc;
	TreeHeader* th;
	RFNode* nodes;
	CompactLeaf* compactLeaves;
	uchar* histograms;
};

struct EnsembleHeader
{
	int numTrees;
	int treeAllocSize;
};


#endif DEFINES


#define MAXMIN(x,lower,upper) ceil(std::max(std::min((float)(x),(float)(upper)),(float)(lower)))
#define OFFLIMITS(x,lower,upper) ((x)<(lower) || (x)>(upper))
#define OFFLIMITS2(x,lx,ux,y,ly,uy) ( OFFLIMITS(x,lx,ux) || OFFLIMITS(y,ly,uy) )

using namespace std;

namespace ZSHand
{
	struct Outputs
	{
		char* ensemble;

		TreeHeader th;
		int numTrees;
		int frameWidth;
		int frameHeight;

		uchar * labels;
		uint * histograms;
		float * weights;
		uint * leaves;
	};

	void loadTree(Tree &tree)
	{
		tree.th =				(TreeHeader*)	(tree.treeAlloc);
		tree.nodes =			(RFNode*)		((char*)tree.th				+ sizeof(TreeHeader));
		tree.compactLeaves =	(CompactLeaf*)	((char*)tree.nodes			+ sizeof(RFNode)* tree.th->totalNodes);
		tree.histograms =		(uchar*)		((char*)tree.compactLeaves	+ sizeof(CompactLeaf)* tree.th->totalHists);	
	}

	Error loadTree(std::string treeName, Tree &tree)
	{	
		std::ifstream ifs(treeName.c_str(), std::ios::binary);

		// Open check
		if (!ifs.is_open())
			return FILE_NOT_OPEN;

		TreeHeader th;
		ifs.read((char*)&th, sizeof(TreeHeader));
		ifs.seekg(0,std::ios_base::beg);

		int allocSize = sizeof(TreeHeader) + sizeof(RFNode)*th.totalNodes + sizeof(CompactLeaf)*th.totalHists + sizeof(uchar)*th.totalHists*th.numClasses;
		tree.treeAlloc = new char[allocSize];  

		// Memory allocation check
		if (!tree.treeAlloc)
			return OUT_OF_MEMORY;

		ifs.read((char*)tree.treeAlloc,allocSize);	

		loadTree(tree);

		ifs.close();
		return NO_ERROR;
	}

	void deleteTree(Tree &tree)
	{
		delete[] tree.treeAlloc;
	}

	Error Ensemble::LoadEnsemble(HND& hnd, char* filename)
	{
		if (hnd) 
			return ENSEMBLE_ALREADY_LOADED;
		hnd = (HND)(new Outputs);
		Outputs& O = *((Outputs*)hnd);

		O.ensemble = 0; O.labels = 0; O.histograms = 0; O.leaves = 0; O.weights = 0;
		O.frameHeight = 0; O.frameWidth = 0;

		ostringstream ss;	
		string treeBase(filename);
		string treeName; 

		// Count available trees
		int numTrees = 0;
		while (1)
		{
			ss.str("");
			ss << setw(4) << setfill('0') << numTrees;
			treeName = treeBase + "Tree_" + ss.str() + ".tree";

			std::ifstream ifs(treeName.c_str(), std::ios::binary);

			if (!ifs.is_open())
				break;

			ifs.close();

			numTrees++;
		}

		if (!numTrees)
			return NO_DATA_FILES;

		// Get a tree header to extract sizes
		treeName = treeBase + "Tree_0000.tree";

		ifstream ifs(treeName.c_str(), ios::binary);

		TreeHeader th;
		ifs.read((char*)&th,sizeof(TreeHeader));
		ifs.close();

		printf("Depth: %i\n", th.depth);
		printf("Number of classes: %i\n", th.numClasses);
		printf("Number of nodes: %i\n", th.totalNodes);
		printf("Number of histograms: %i\n", th.totalHists);

		int totalNodes = (int)pow(2.0f, th.depth + 1) - 1; // All the nodes sum to this number
		int totalHists = 2*totalNodes + 1; // There are 2 histograms under every node and one above the root node

		th.totalNodes = totalNodes;
		th.totalHists = totalHists;

		int allocSize = sizeof(TreeHeader) + sizeof(RFNode)*totalNodes + sizeof(CompactLeaf)*totalHists + sizeof(uchar)*totalHists*th.numClasses;
		int EnsembleAllocSize = sizeof(EnsembleHeader) + allocSize*numTrees;

		printf("Ensemble allocation size: %f mb\n", EnsembleAllocSize/1e6);


		// Allocate memory for the ensemble
		char* EnsembleAlloc = new char[EnsembleAllocSize];
		O.ensemble = EnsembleAlloc;
		O.th = th;
		O.numTrees = numTrees;

		if (!EnsembleAlloc)
			return OUT_OF_MEMORY;

		EnsembleHeader fh;
		fh.numTrees = numTrees;
		fh.treeAllocSize = allocSize;

		*(EnsembleHeader*)EnsembleAlloc = fh;
		EnsembleAlloc += sizeof(EnsembleHeader);

		// Load each tree file into the ensemble
		for (int t = 0; t < numTrees; t++)
		{       
			ss.str("");
			ss << setw(4) << setfill('0') << t;
			treeName = treeBase + "Tree_" + ss.str() + ".tree";
			printf("Loading tree: %s\n", treeName.c_str());
			Tree tree;
			int res = loadTree(treeName, tree);

			memcpy(EnsembleAlloc, &th, sizeof(TreeHeader));    

			EnsembleAlloc+=sizeof(TreeHeader);
			memcpy(EnsembleAlloc, tree.nodes,  sizeof(RFNode)*th.totalNodes); 

			EnsembleAlloc+=(sizeof(RFNode)*th.totalNodes);
			memcpy(EnsembleAlloc, tree.compactLeaves, sizeof(CompactLeaf)* th.totalHists); 

			EnsembleAlloc+=(sizeof(CompactLeaf)* th.totalHists);
			memcpy(EnsembleAlloc, tree.histograms, sizeof(uchar)*th.totalHists*th.numClasses); 

			EnsembleAlloc+=(sizeof(uchar)*th.totalHists*th.numClasses);

			deleteTree(tree);
		}
		return NO_ERROR;
	}

	Error Ensemble::RemoveEnsemble(HND& hnd)
	{
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		delete []O.ensemble;
		delete []O.histograms;
		delete []O.labels;
		delete []O.leaves;
		delete []O.weights;

		delete (Outputs*)hnd;

		hnd = 0;

		return NO_ERROR;
	}

	Error Ensemble::EnableOutputs(HND hnd, Flags flags, int width, int height)
	{
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		if (width<=0 || height<=0 || O.th.numClasses<=0)
			return ZERO_DIMENSION;

		O.frameHeight = height;
		O.frameWidth = width;

		if ((flags & LABEL) && !O.labels) 
			O.labels = new uchar[O.frameWidth*O.frameHeight]; 

		if ((flags & HISTOGRAM) && !O.histograms) 
			O.histograms = new uint[O.frameWidth*O.frameHeight*O.th.numClasses]; 

		if ((flags & WEIGHTS) && !O.weights) 
			O.weights = new float[O.frameWidth*O.frameHeight]; 

		if ((flags & LEAF) && !O.leaves) 
			O.leaves = new uint[O.frameWidth*O.frameHeight*O.numTrees]; 

		return NO_ERROR;
	}

	Error Ensemble::DisableOutputs(HND hnd, Flags flags)
	{
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		if (flags &         LABEL) delete[] O.labels;
		if (flags &     HISTOGRAM) delete[] O.histograms;			
		if (flags &       WEIGHTS) delete[] O.weights;			
		if (flags &          LEAF) delete[] O.leaves;

		return NO_ERROR;
	}

	struct t4
	{
		int x1,y1,x2,y2;
	};

	Error Ensemble::ApplyEnsemble(HND hnd, ushort* img, int maxDepth)
	{		
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		int rows = O.frameHeight;
		int cols = O.frameWidth;

		EnsembleHeader fh = *(EnsembleHeader*)O.ensemble;
		TreeHeader th = *(TreeHeader*)(O.ensemble + sizeof(EnsembleHeader));
		int hsz = (fh.numTrees * th.numClasses);
		uint *H = new uint[hsz*32]; // 32 is maximum threads
		if (!H) 
			return OUT_OF_MEMORY;

		// Clear memory
		if (O.labels)         memset(O.labels,       th.numClasses,  rows*cols*sizeof(uchar));
		if (O.leaves)         memset(O.leaves,           0,          rows*cols*O.numTrees*sizeof(uint));
		if (O.histograms)     memset(O.histograms,       0,          rows*cols*th.numClasses*sizeof(uint));
		if (O.weights)        memset(O.weights,          0,          rows*cols*sizeof(float));


		float tot=0;
		//Max feature value
		float maxDiff = 200.0;
		float maxVal = 255.0;
		float fMAX = floorf(maxVal/2.0-1.0);
		//Maximum value of a z value in the depth image

		// omp_set_num_threads(4);
		//#pragma omp parallel for 
		for (int r = 0; r < rows; r++)
			for (int c = 0; c < cols; c++)			
				if (img[r*cols + c] != 0)
				{
					// We now classify a point
					int depth = img[r*cols + c];

					int th_id;   
					// If we use OMP We can now calculate the histograms independently without overwriting the others
					// Without the extended histogram there is severe noising
					th_id = omp_get_thread_num(); 
					uint * h = H + th_id*hsz;
					memset(h, 0, fh.numTrees*th.numClasses*sizeof(uint));
					for (int t = 0; t < fh.numTrees; t++)
					{
						Tree tree;
						tree.treeAlloc = O.ensemble + sizeof(EnsembleHeader) + t*fh.treeAllocSize;
						loadTree(tree);
						if (maxDepth > tree.th->depth)
							maxDepth = tree.th->depth;

						uint leaf = 0;

						int rd=r*depth;
						int cd=c*depth;
						t4 t_;
						int &x1=t_.x1;
						int &y1=t_.y1;
						int &x2=t_.x2;
						int &y2=t_.y2;
						
						//Find leaf node for tree q
						for (int d = 0; d <= maxDepth ; d++)
						{
							int x1 = (r + float(tree.nodes[leaf].x1)/depth); //r and x is the row
							int y1 = (c + float(tree.nodes[leaf].y1)/depth); //c and y is the col
							int x2 = (r + float(tree.nodes[leaf].x2)/depth); //r and x is the row
							int y2 = (c + float(tree.nodes[leaf].y2)/depth); //c and y is the col

							float d1,d2;

							if OFFLIMITS2(x1,0,rows-1,y1,0,cols-1) d1 = 0; else d1 = img[x1*cols + y1];
							if OFFLIMITS2(x2,0,rows-1,y2,0,cols-1) d2 = 0; else d2 = img[x2*cols + y2];

							float feat;
							if (d1 == 0 && d2 == 0)	feat = 0;
							else if (d1  == 0) feat = fMAX;	
							else if (d2  == 0) feat = -fMAX;	
							else
							{
								// this maps a value from [-1,1] to [-fMAX, fMAX]
								feat = (d1 - d2) / maxDiff * fMAX;            
								feat = MAXMIN(feat,-fMAX,fMAX);	
							}

							leaf = leaf*2 + uint(feat > tree.nodes[leaf].thr) + 1;
						}
						if (O.leaves)
							O.leaves[r*cols + c + t*rows*cols] = leaf;

						for (int C = 0; C < th.numClasses; C++)
							h[C + t*th.numClasses] = (float)tree.histograms[leaf*th.numClasses + C]/255.0f*(float)tree.compactLeaves[leaf].maxVal; ///255.0 here is because we compressed everything to a byte
						//H[tree.compactLeaves[leaf].label + t*th.numClasses] = tree.compactLeaves[leaf].maxVal;
					}

					uchar label = 255;
					float best = 0, sumH, sumTot = 0; 
					for (int C = 0; C < th.numClasses; C++) 
					{
						sumH = 0;
						for (int t = 0; t < fh.numTrees; t++)
							sumH += h[C + t*th.numClasses]; // maybe this should be weighted by entropy? OR it could be used with a max instead of mean ...

						if (sumH > best) {                        
							label = C;
							best = sumH;
						}

						sumTot += sumH;

						if (O.histograms)
							O.histograms[r*cols + c + C*rows*cols] = sumH;						
					}

					if (O.weights)						
						O.weights[r*cols + c]=sumTot;

					if (O.labels) 
						O.labels[r*cols + c] = label;
				}

				delete[] H;
		return NO_ERROR; // compatibility: original omitted nonvoid return
	}

	Error Ensemble::GetLabels(HND hnd, uchar* &img)
	{
		img = 0;
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		img = O.labels;
		return NO_ERROR;
	}

	Error Ensemble::GetHistograms(HND hnd, uint* &img)
	{
		img = 0;
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		img = O.histograms;
		return NO_ERROR;
	}

	Error Ensemble::GetWeights(HND hnd, float* &img)
	{
		img = 0;
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		if (!hnd || !O.weights)
			return ENSEMBLE_NOT_LOADED;

		img = O.weights;

		return NO_ERROR;
	}

	Error Ensemble::GetLeaves(HND hnd, uint* &img)
	{
		img = 0;
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		img = O.leaves;
		return NO_ERROR;
	}

	Error Ensemble::GetNumClasses(HND hnd, int& numClasses)
	{
		numClasses = 0;
		Outputs& O = *((Outputs*)hnd);
		if (!hnd || !O.ensemble) 
			return ENSEMBLE_NOT_LOADED;

		numClasses = O.th.numClasses;
		return NO_ERROR;
	}

}



