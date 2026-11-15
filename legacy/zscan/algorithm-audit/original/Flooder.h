#include <math.h>
#include <string.h>
#include <iostream>
#include "BinaryHeap.h"
#include "opencv2/opencv.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/features2d/features2d.hpp"

#define SQR(x) ((x)*(x))
#define IND(r, c) ((r)*width+(c))
#define MAX(x, y) (((x)>(y))?(x):(y))
#define MIN(x, y) (((x)<(y))?(x):(y))
#define DBG(x) {printf("-%i-\n",x);}

using namespace std;

struct Pos {
public:
	int row, col, width, height, index;
	float *grid;
	float getT() const { return grid[row*width+col];}
	Pos() :row(-1), col(-1), width(-1), height(-1), grid(0), index(0){}
	Pos(int r, int c, int w, int h, float *g) : row(r), col(c), width(w), height(h), grid(g), index(0) {}

	bool operator<(const Pos& rhs) const  {return  getT()<rhs.getT();}
};

class Flooder
{
public:
	Pos *GREEN, *BURNT;
	int width,height;
	float *T, *F, *pre3d;
	Pos* *S;

	Flooder(float *F, int w, int h,float *pred3d);
	~Flooder();

	//void flood(int r0,int c0, float lamda);
	void flood3d(int r0,int c0, float lamda);
	void setMap(float* F) { this->F = F;}
};

Flooder::Flooder(float* F, int w, int h, float *pred3d = 0): width(w),height(h) 
{
	GREEN = new Pos(); 
	BURNT = new Pos();
	T = new float[width*height];
	S = new Pos*[width*height];
	this->F = F;
	this->pre3d = pred3d;
}

Flooder::~Flooder()
{
	delete GREEN;
	delete BURNT;
	delete[] T;
	delete[] S;
}

void Flooder::flood3d(int r0,int c0, float lamda)
{
	BinaryHeap<Pos> Q;            

	Pos *u,*s,*tmp;

	float inf = 1e30;
	for (int i = 0; i < width*height; i++)
	{
		S[i]=GREEN;
		T[i]=inf;
	}

	T[IND(r0,c0)] = 0;

	//Add first position to the Q
	tmp = new Pos(r0, c0, width, height, T);
	Q.insert(tmp);
	S[IND(r0, c0)] = BURNT;

	int len = 4;
	int R[] = {-1, 1, 0, 0, -1, 1, -1,  1};
	int C[] = {0, 0, -1, 1, -1, 1,  1, -1};

	float x0,y0,z0,x1,y1,z1,cost;

	int count = 0;
	while (Q.size())
	{
		//compare might need changing around
		Q.deleteMin(u); //Stores smallest element in u and removes it from Q

		float T0 = T[IND(u->row, u->col)];
		if (T0 > 1e20 )
			break;

		S[IND(u->row, u->col)] = BURNT;

		float *pPre = pre3d + IND(u->row, u->col)*3;
		float z = F[IND(u->row, u->col)];

		x0 = (*pPre)*z; pPre++;
		y0 = (*pPre)*z; pPre++;
		z0 = (*pPre)*z;

		for (int ind = 0; ind < len; ind++)
		{
			int r = u->row + R[ind];
			int c = u->col + C[ind];

			if ((r>=0 && r<height)&&(c>=0 && c<width))
			{
				if (S[IND(r, c)] != BURNT)
				{
					float *pPre = pre3d + IND(r, c)*3;
					float z = F[IND(r, c)];

					x1 = (*pPre)*z; pPre++;
					y1 = (*pPre)*z; pPre++;
					z1 = (*pPre)*z;
					cost = sqrt(SQR(x0 - x1) + SQR(y0 - y1) + SQR(z0 - z1));

					if (cost < lamda)
					{
						T[IND(r, c)] = MIN(T[IND(r, c)], T0 + cost);
						if (S[IND(r, c)]==GREEN)
						{
							tmp = new Pos(r, c, width, height, T);
							S[IND(r, c)] = Q.insert(tmp);
						}
						else  //It is burning
							Q.reorder(S[IND(r, c)]);
					}
				}
			}
		}
		delete u;
	}
}
