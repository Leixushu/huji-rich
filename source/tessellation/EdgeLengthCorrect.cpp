#include "EdgeLengthCorrect.hpp"

void CorrectEdgeLength(Tessellation const& tessold,Tessellation const& tessnew,
	vector<double> &lengths)
{
	int n=tessold.GetTotalSidesNumber();
	int npoints=tessold.GetPointNo();
	lengths.resize(n);
	for(int i=0;i<npoints;++i)
	{
		vector<int> edgesold=tessold.GetCellEdges(i);
		vector<int> edgesnew=tessnew.GetCellEdges(i);
		int nedges=(int)edgesold.size();
		int nedgesnew=(int)edgesnew.size();
		for(int j=0;j<nedges;++j)
		{
			Edge const& edge=tessold.GetEdge(edgesold[j]);
			int n0=edge.GetNeighbor(0);
			int n1=edge.GetNeighbor(1);
			bool found=false;
			// Don't do double work
			if((n0<i&&n0>=0)||(n1<i&&n1>=0))
				continue;
			for(int k=0;k<nedgesnew;++k)
			{
				Edge const& edgenew=tessnew.GetEdge(edgesnew[(k+j)%nedgesnew]);
				// are the two edges the same?
				if(((tessold.GetOriginalIndex(n0)==tessnew.GetOriginalIndex(
					edgenew.GetNeighbor(0)))&&
					(tessold.GetOriginalIndex(n1)==tessnew.GetOriginalIndex(
					edgenew.GetNeighbor(1))))||((tessold.GetOriginalIndex(n0)==
					tessnew.GetOriginalIndex(edgenew.GetNeighbor(1)))&&
					(tessold.GetOriginalIndex(n1)==tessnew.GetOriginalIndex(
					edgenew.GetNeighbor(0)))))
				{
					lengths[edgesold[j]]=0.5*(edge.GetLength()+edgenew.GetLength());
					found=true;
					break;
				}
			}
			if(!found)
				lengths[edgesold[j]]=0.5*edge.GetLength();
		}
	}
}


void CorrectEdgeLength(Tessellation const& tessold,Tessellation const& tessmid,
	Tessellation const& tessnew,vector<double> &lengths)
{
	int n=tessmid.GetTotalSidesNumber();
	int npoints=tessmid.GetPointNo();
	lengths.resize(n);
	for(int i=0;i<npoints;++i)
	{
		vector<int> edgesold=tessold.GetCellEdges(i);
		vector<int> edgesnew=tessnew.GetCellEdges(i);
		vector<int> edgesmid=tessmid.GetCellEdges(i);
		int nedges=(int)edgesold.size();
		int nedgesnew=(int)edgesnew.size();
		int nedgesmid=(int)edgesmid.size();
		for(int j=0;j<nedgesmid;++j)
		{
			Edge const& edge=tessmid.GetEdge(edgesmid[j]);
			int n0=edge.GetNeighbor(0);
			int n1=edge.GetNeighbor(1);
			// Don't do double work
			if((n0<i&&n0>=0)||(n1<i&&n1>=0))
				continue;
			lengths[edgesmid[j]]=0.5*edge.GetLength();
			for(int k=0;k<nedgesnew;++k)
			{
				Edge const& edgenew=tessnew.GetEdge(edgesnew[(k+j)%nedgesnew]);
				// are the two edges the same?
				if(((tessmid.GetOriginalIndex(n0)==tessnew.GetOriginalIndex(
					edgenew.GetNeighbor(0)))&&
					(tessmid.GetOriginalIndex(n1)==tessnew.GetOriginalIndex(
					edgenew.GetNeighbor(1))))||((tessmid.GetOriginalIndex(n0)==
					tessnew.GetOriginalIndex(edgenew.GetNeighbor(1)))&&
					(tessmid.GetOriginalIndex(n1)==tessnew.GetOriginalIndex(
					edgenew.GetNeighbor(0)))))
				{
					lengths[edgesmid[j]]+=0.25*edgenew.GetLength();
					break;
				}
			}
			for(int k=0;k<nedges;++k)
			{
				Edge const& edgeold=tessold.GetEdge(edgesold[(k+j)%nedges]);
				// are the two edges the same?
				if(((tessmid.GetOriginalIndex(n0)==tessold.GetOriginalIndex(
					edgeold.GetNeighbor(0)))&&
					(tessmid.GetOriginalIndex(n1)==tessold.GetOriginalIndex(
					edgeold.GetNeighbor(1))))||((tessmid.GetOriginalIndex(n0)==
					tessold.GetOriginalIndex(edgeold.GetNeighbor(1)))&&
					(tessmid.GetOriginalIndex(n1)==tessold.GetOriginalIndex(
					edgeold.GetNeighbor(0)))))
				{
					lengths[edgesmid[j]]+=0.25*edgeold.GetLength();
					break;
				}
			}
		}
	}
}