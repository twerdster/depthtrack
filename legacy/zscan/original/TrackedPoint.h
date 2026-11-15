#include "opencv2/opencv.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/features2d/features2d.hpp"

using namespace cv;

const float inf = 1e30;

class TrackedPoint : public std::deque<Point3f>
{
public:
	float matchedDist;
	bool matched; // Matched occurs when a match is found
	int unmatchedCnt, age;
	KalmanFilter KF;
	Mat_<float> measurement;

	TrackedPoint(int length):matched(true),unmatchedCnt(0),age(0), matchedDist(inf) 
	{
		KF.init(6,3,0);
		KF.transitionMatrix = *(Mat_<float>(6, 6) << 1,0,0,1,0,0,  0,1,0,0,1,0,  0,0,1,0,0,1,  0,0,0,1,0,0,  0,0,0,0,1,0,  0,0,0,0,0,1 );
		measurement = Mat_<float>(3,1); 
		measurement.setTo(Scalar(0));
	}

	void updateWithKalman(int agelim)
	{
		if (age==agelim)
		{
			Point3f p=back();
			// init...
			KF.statePre.at<float>(0) = p.x;
			KF.statePre.at<float>(1) = p.y;
			KF.statePre.at<float>(2) = p.z;
			KF.statePre.at<float>(3) = 0;
			KF.statePre.at<float>(4) = 0;
			KF.statePre.at<float>(5) = 0;
			
			setIdentity(KF.measurementMatrix);
			setIdentity(KF.processNoiseCov, Scalar::all(1e-4));
			setIdentity(KF.measurementNoiseCov, Scalar::all(5e-3));
			setIdentity(KF.errorCovPost, Scalar::all(.2));
		} else if (age>agelim)
		{				
			// First predict, to update the internal statePre variable
			Mat prediction = KF.predict();
			Point3f predictPt(prediction.at<float>(0),prediction.at<float>(1),prediction.at<float>(3));

			// Get 3d point
			Point3f measPt(back());
			measurement(0)=measPt.x;
			measurement(1)=measPt.y;
			measurement(2)=measPt.z;

			// The "correct" phase that is going to use the predicted value and our measurement
			Mat estimated = KF.correct(measurement);
			//Point statePt(estimated.at<float>(0),estimated.at<float>(1),estimated.at<float>(2));
			back()=Point3f(estimated.at<float>(0),estimated.at<float>(1),estimated.at<float>(2));
		}
	}
};

class TrackingTrigger
{
public:
	virtual bool trigger(Point3f p3d) = 0; 
};

// Class for triggering tracking when a point is behind a plane
class PlaneTrigger : public TrackingTrigger
{
public:
	PlaneTrigger(float a, float b, float c, float d):a_(a),b_(b),c_(c),d_(d){};
	bool trigger(Point3f p3d) { return p3d.x*a_+p3d.y*b_+p3d.z*c_ > 0; }
private:
	float a_,b_,c_,d_;
};

// Class for triggering tracking when a point is within a sphere
class SphereTrigger : public TrackingTrigger
{
public:
	SphereTrigger(Point3f ctr, float r):ctr_(ctr),r_(r){};
	bool trigger(Point3f p3d) { return (ctr_.x-p3d.x)*(ctr_.x-p3d.x)+(ctr_.y-p3d.y)*(ctr_.y-p3d.y)+(ctr_.z-p3d.z)*(ctr_.z-p3d.z) < r_*r_; }
	void setCtr(Point3f ctr, float r) { ctr_=ctr; r_=r;}
private:
	Point3f ctr_;
	float r_;
};



/*
Tracker Class
-------------
Used to accept observations and track over frames.
Sample usage:

// Setup tracker
Tracker t; 
t.addTrackingTrigger(new SphereTrigger(0,300,0,200));
t.setMatchDistance(120.0f); // point needs to be within 120 of last track point
t.setUnmatchedDeath(10); // points unmatched for more than 10 frames are killed
t.setHistoryLength(20); // 20 points are maintained in a tracks history
t.setMaxTrackedPoints(2); // up to two tracks can be maintained
t.setTrackAll(false); // When this is unset then only triggers can activate a track, otherwise any observation can

// New Observation
t.update(pts);

// Go over tracks
for (int i=0; i<t.numTrackedPoints(); ++i)
{
TrackedPoint &tp = t.at(i); // access track i
int age = t.age(i); // OR tp.age // get age of track i
Point3f pp = t.predict(i); // predict next position for track i
}

// Manually delete tracks
t.kill(1); // delete track 1
*/

class Tracker
{
public:
	Tracker():deltaTime_(0.033),maxPts_(2),length_(20),unmatched_(20),
		idleframe_(40), matchRadius_(60*60), trackAll_(false), canInitiate_(true), canAge_(true){}
	~Tracker();

	void addTrackingTrigger(TrackingTrigger *tt) {trackingTriggers.push_back(tt);} //starts tracking a specific point if it passes through a trigger space
	void addKillTrigger(TrackingTrigger *tt) {killTriggers.push_back(tt); } // allows a point to get destroyed if it is in a specific trigger area

	void setMatchDistance(const float dist) { matchRadius_ = dist*dist; }; // Distance between predicted position and observed for match
	//t.setMatcher(Tracker::Point2Any\Tracker::Point2Point\Tracker::) // I.e. find match between point and closests tracked point OR find matche between tracked point and closest observed point
	void update(cv::Mat pts); // absorbs the new observations and matches them to tracked points and updates positions	
	void kill(int i) { tpv.erase(tpv.begin()+i); } // kills tracked point

	int age(int i) { return tpv[i].age; }
	int numTrackedPoints() { return tpv.size(); }
	TrackedPoint& at(int i) { return tpv[i];}

	void setIdleFrameDeath(int frames) { idleframe_=frames;} // After frames idle frames kill a point
	//void setIdleTimeDeath(float time); // After specified idle time kill a point
	void setUnmatchedDeath(int frames) { unmatched_=frames;} // After 10 consecutive unmatched frames kill a point
	//void setVarianceDeath(Point3f(75,75,75), 10); // If trajectory exibits instability of magnitude 75 in all axes for 10 frames kill it 

	//void useFilter(Tracker::NONE\Tracker::Average\Tracker::KALMAN);
	Point3f predict(int i);

	//void useTimeType(Tracker::CLOCK\Tracker::DELTA); // choose between using the actual system clock or a fixed time delta for computing speeds etc
	void setTimeDelta(float deltaTime) {deltaTime_=deltaTime;} // Used for accurate 
	void setHistoryLength(int length) {length_=length;} // Number of previous frames to maintain
	void setMaxTrackedPoints(int maxPts) {maxPts_=maxPts;} // Cannot track more than this number
	void setTrackAll(bool trackAll) { trackAll_=trackAll;} // If this is set it tracks all observations 
	void setCanInitiate(bool can) { canInitiate_=can;} // If this is not set then no tracks can be initiated 
	void setCanAge(bool can) {canAge_=can;} // If this is set then age will be incremented every time

	std::vector<TrackedPoint> tpv; // vector of tracked points. Maybe should be a linked list for better insertion and deletion complexity?
	std::vector<TrackingTrigger*> trackingTriggers; // vector of tracking triggers
	std::vector<TrackingTrigger*> killTriggers; // vector of kill triggers

public: // Visibility obviously needs correcting
	float deltaTime_, matchRadius_;
	int maxPts_, length_, unmatched_,idleframe_;
	bool trackAll_,canInitiate_,canAge_;
};

Tracker::~Tracker() 
{
	for (int i=0; i<trackingTriggers.size(); ++i) 
		delete trackingTriggers[i];

	for (int i=0; i<killTriggers.size(); ++i) 
		delete killTriggers[i];
}

Point3f Tracker::predict(int i) 
{
	// Should include kalman predictor\filter. For no filtering can reduce the prediction to:
	return tpv[i].back(); 
}

void Tracker::update(cv::Mat pts)
{
	// NOTE: when a point goes unmatched do you still need to predict its position and add the predicted position?

	// Set all current tracks to be unmatched
	for (int i=0; i<tpv.size();++i)
	{
		tpv[i].matched=false; // Initialized each update as unmatched and potentially updated during matching
		tpv[i].matchedDist=inf; // Used to find the best match out of the possible matched observations
		if (canAge_) tpv[i].age++; // Every update increases age by 1. This never goes down.
		if (canAge_) tpv[i].unmatchedCnt++; // In the same way that matched is set to false so is the continuous unmatched count. It will be reset when a match is found
	}

	// Now perform matching by going over every observation
	for (int i=0; i<pts.rows;++i)
	{
		cv::Point3f p3d = pts.at<cv::Point3f>(i,0); // Get the observation location
		int match = -1; // Assume no matches
		float mindist = inf;
		// Go over all tracks 
		for (int j=0; j<tpv.size(); ++j)
			if (tpv[j].age>0) // i.e. if the tracked point hasnt been added now
			{
				Point3f pred = predict(j);
				float d = (pred.x-p3d.x)*(pred.x-p3d.x)+(pred.y-p3d.y)*(pred.y-p3d.y)+(pred.z-p3d.z)*(pred.z-p3d.z); 
				if (d < matchRadius_ && d < mindist) // if match is within matching distance then update 
				{
					match = j;
					mindist = d;
				}
			}

			// If the detected point is not matched to any track
			if (match < 0) 			
			{
				if (tpv.size()<maxPts_ && canInitiate_)
				{
					std::vector<TrackingTrigger*>::iterator it = trackingTriggers.begin();
					bool triggered = false | trackAll_ ;

					while ( it < trackingTriggers.end() && !triggered) triggered |= (*(it++))->trigger(p3d);

					if (triggered) // If the detected point is within a triggerspace or trackAll is set then add it as a new point to track
					{
						TrackedPoint tp(length_);
						tp.push_back(p3d);  
						tpv.push_back(tp);
					}
				}
			}
			else // otherwise it is matched to a tracked point (if we went the other way round and matched tracked points to observations we might end up with merged tracks)
			{
				// we might have already updated the point so check to see if this match will be better
				if (mindist < tpv[match].matchedDist )
				{
					// If this track has already been matched then just update the match
					if (tpv[match].matched)
						tpv[match].back() = p3d;
					else // otherwise add it on to the history
					{
						tpv[match].push_back(p3d);
						tpv[match].unmatchedCnt=0;
					}

					tpv[match].matchedDist = mindist;
					tpv[match].matched=true;

					// If there are too many points pop the front off
					while (tpv[match].size()>length_)
						tpv[match].pop_front();
				}
			}
	}

	// Go through kill triggers and also unmatched counts in order to remove tracks
	for (std::vector<TrackedPoint>::iterator it = tpv.begin(); it !=tpv.end();)
	{
		Point3f p3d =(*it).back();
		std::vector<TrackingTrigger*>::iterator itkill = killTriggers.begin();
		bool triggered = false;
		while ( itkill < killTriggers.end() && !triggered) triggered |= (*(itkill++))->trigger(p3d);
		triggered |= (*it).unmatchedCnt > unmatched_;

		if (triggered) 
			it=tpv.erase(it);
		else
			it++;
	}
}

//NOTE: need refine detection location usinf fmm and heuristics, filter detections using the area of the connected components


