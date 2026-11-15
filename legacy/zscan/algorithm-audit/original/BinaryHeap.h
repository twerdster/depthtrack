#ifndef BINARY_HEAP_H_
#define BINARY_HEAP_H_

#include <vector>
using namespace std;

template <class IndexedComparable>
        class BinaryHeap {
        public:
            BinaryHeap( );
            BinaryHeap( const vector<IndexedComparable*> & v );
            
            bool isEmpty( ) const;
            const IndexedComparable* & findMin( ) const;
            
            IndexedComparable* & insert(  IndexedComparable* & x );
            IndexedComparable* & reorder(  IndexedComparable* & x);
            void deleteMin( );
            void deleteMin( IndexedComparable* & minItem );
            void makeEmpty( );
            int size() {return theSize;}
            ~BinaryHeap();
            
        private:
            int  theSize;  // Number of elements in heap
            vector<IndexedComparable*> array;    // The heap array of objects
            
            
            void buildHeap( );
            void percolateDown( int hole );
};


//IndexComparable has the operator< defined as well as index defined.

// Construct the binary heap.
template <class IndexedComparable>
BinaryHeap<IndexedComparable>::BinaryHeap( )
: array( 11 ), theSize( 0 ) {
}

// Construct the binary heap.
// v is a vector containing the initial items.
template <class IndexedComparable>
BinaryHeap<IndexedComparable>::BinaryHeap( const vector<IndexedComparable*> & v )
: array( v.size( ) + 1 ), theSize( v.size( ) ) {
	array[0]=0;
	for( int i = 0; i < v.size( ); i++ ) {
		array[ i + 1 ] = v[ i ];
		array[i+1]->index=i+1;
	}
	buildHeap( );
}

// Insert item x into the priority queue, maintaining heap order.
// Duplicates are allowed.
template <class IndexedComparable>
IndexedComparable* & BinaryHeap<IndexedComparable>::insert(  IndexedComparable* & x ) {
	array[ 0 ] = x;   // initialize sentinel
	array[0]->index=0;
	if( theSize + 1 == array.size( ) )
		array.resize( array.size( ) * 2 + 1 );

	// Percolate up
	int hole = ++theSize;
	for( ; *x < *array[ hole / 2 ]; hole /= 2 ) {
		array[ hole ] = array[ hole / 2 ];
		array [hole]->index=hole;
	}

	array[ hole ] = x;
	array[hole]->index=hole;
	return array[hole];
}

// IReorder item x in the priority queue to ensure that the element is correctly positioned
//, maintaining heap order.

template <class IndexedComparable>
IndexedComparable* & BinaryHeap<IndexedComparable>::reorder( IndexedComparable* & X ) {
	if( isEmpty( ) ) printf("UnderFlow in BinaryHeap\n");

	int x=X->index;

	IndexedComparable *tmp=array[x];            

	//Delete the element
	array[ x ] = array[ theSize--];
	array[x]->index=x;
	percolateDown( x );

	//Reinsert
	return insert(tmp);
}


// Find the smallest item in the priority queue.
// Return the smallest item, or throw UnderflowException if empty.
template <class IndexedComparable>
const IndexedComparable* & BinaryHeap<IndexedComparable>::findMin( ) const {
	if( isEmpty( ) ) printf("UnderFlow in BinaryHeap\n");

	return array[ 1 ];
}

// Remove the smallest item from the priority queue.
// Throw UnderflowException if empty.
//This procedure deletes the memory itself.
template <class IndexedComparable>
void BinaryHeap<IndexedComparable>::deleteMin( ) {
	if( isEmpty( ) ) printf("UnderFlow in BinaryHeap\n");
	delete array[1];
	array[ 1 ] = array[ theSize-- ];
	if (theSize>0){
		array[1]->index=1;
		percolateDown( 1 );
	}//otherwise heap empty
}



// Remove the smallest item from the priority queue
// and place it in minItem. Throw UnderflowException if empty.

//This procedure puts the responsibility on the caller to delete the memory.
template <class IndexedComparable>
void BinaryHeap<IndexedComparable>::deleteMin( IndexedComparable*  & minItem ) {
	if( isEmpty( ) ) printf("UnderFlow in BinaryHeap\n");

	minItem = array[1];
	array[ 1 ] = array[ theSize-- ];
	array[1]->index=1;
	percolateDown( 1 );
}

// Establish heap-order property from an arbitrary
// arrangement of items. Runs in linear time.
template <class IndexedComparable>
void BinaryHeap<IndexedComparable>::buildHeap( ) {
	for( int i = theSize / 2; i > 0; i-- )
		percolateDown( i );
}

// Test if the priority queue is logically empty.
// Return true if empty, false otherwise.
template <class IndexedComparable>
bool BinaryHeap<IndexedComparable>::isEmpty( ) const {
	return theSize == 0;
}

// Make the priority queue logically empty.
template <class IndexedComparable>
void BinaryHeap<IndexedComparable>::makeEmpty( ) {
	theSize = 0;
}

// Internal method to percolate down in the heap.
// hole is the index at which the percolate begins.
template <class IndexedComparable>
void BinaryHeap<IndexedComparable>::percolateDown( int hole ) {
	int child;
	IndexedComparable* tmp = array[ hole ];

	for( ; hole * 2 <= theSize; hole = child ) {
		child = hole * 2;
		if( child != theSize && *array[ child + 1 ] < *array[ child ] )
			child++;
		if( *array[ child ] < *tmp ) {
			array[ hole ] = array[ child ];
			array[hole]->index=hole;
		}
		else
			break;
	}
	array[ hole ] = tmp;
	array[hole]->index=hole;
}

template <class IndexedComparable>
BinaryHeap<IndexedComparable>::~BinaryHeap( )
{
	for (int i=1;i<=theSize;i++)
		delete array[i];
}


#endif
