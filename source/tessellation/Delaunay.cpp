#include "Delaunay.hpp"
#include <vector>
#include <cmath>

Delaunay::DataOnlyForBuild::DataOnlyForBuild():insert_order(vector<int> ()),
	copied(vector<vector<char> > ())
{}

Delaunay::DataOnlyForBuild::DataOnlyForBuild(DataOnlyForBuild const& other):
insert_order(other.insert_order),copied(other.copied){}

Delaunay::DataOnlyForBuild& Delaunay::DataOnlyForBuild::operator=
	(DataOnlyForBuild const& other)
{
	if(this != &other)
	{
		copied=other.copied;
		insert_order=other.insert_order;
	}
	return *this;
}


Delaunay::Delaunay(void): 
	lastFacet(0),CalcRadius(false),
	radius(vector<double>()),cell_points(vector<Vector2D> ()),
	PointWasAdded(false),
	last_facet_added(0),
	f(vector<facet>()),
	cor(vector<Vector2D>()),
	length(0),
	olength(0),location_pointer(0), last_loc(0),
	logger(0) {}

Delaunay::Delaunay(Delaunay const& other):
	lastFacet(other.lastFacet),
	CalcRadius(other.CalcRadius),
	radius(other.radius),cell_points(other.cell_points),
	PointWasAdded(other.PointWasAdded),
	last_facet_added(other.last_facet_added),
	f(other.f),
	cor(other.cor),
	length(other.length),
	olength(other.olength),
	location_pointer(other.location_pointer),
	last_loc(other.last_loc),
	logger(other.logger) {}

Delaunay::~Delaunay(void)
{
	cor.clear();
	f.clear();
	cell_points.clear();
}

namespace{
	// Assume cell is orederd in convexhull counterclockwise
	bool InCell(vector<Vector2D> const& points,Vector2D const& p)
	{
		int n=(int)points.size();
		for(int i=0;i<n;++i)
		{
			if(CrossProduct(points[i]-p,points[(i+1)%n]-p)<0)
				return false;
		}
		return true;
	}

	vector<double> CellSize(vector<Vector2D> const& points)
	{
		int n=(int)points.size();
		double minx=points[0].x;
		double miny=points[0].y;
		double maxx=minx;
		double maxy=miny;
		for(int i=1;i<n;++i)
		{
			minx=min(points[i].x,minx);
			miny=min(points[i].y,miny);
			maxx=max(points[i].x,maxx);
			maxy=max(points[i].y,maxy);
		}
		vector<double> res(4);
		res[0]=minx;
		res[1]=maxx;
		res[2]=miny;
		res[3]=maxy;
		return res;
	}

	int find_index(facet const& fc, int i)
	{
		for(int j=0;j<3;++j)
		{
			if(fc.get_friend(j)==i)
				return j;
		}
		cout<<"Couldn't find number "<<i<<" in facet";
		return 0;
	}
}

void Delaunay::add_point(int index)
{
	int triangle=Walk(index);
	boost::array<int,3> outer,temp_friends;
	facet f_temp;
	int i=0;
	for(i=0;i<3;++i)
	{
		outer[i]=f[triangle].get_vertice(i);
		temp_friends[i]=f[triangle].get_friend(i);
	}
	// create and _update the new facets
	f.push_back(f_temp);
	f.push_back(f_temp);
	f[triangle].set_vertice(outer[2],0);
	f[triangle].set_vertice(outer[0],1);
	f[triangle].set_vertice(index,2);
	f[location_pointer+1].set_vertice(outer[0],0);
	f[location_pointer+1].set_vertice(outer[1],1);
	f[location_pointer+1].set_vertice(index,2);
	f[location_pointer+2].set_vertice(outer[1],0);
	f[location_pointer+2].set_vertice(outer[2],1);
	f[location_pointer+2].set_vertice(index,2);

	f[triangle].set_friend(temp_friends[2],0);
	f[triangle].set_friend(location_pointer+1,1);
	f[triangle].set_friend(location_pointer+2,2);
	f[location_pointer+1].set_friend(temp_friends[0],0);
	f[location_pointer+1].set_friend(location_pointer+2,1);
	f[location_pointer+1].set_friend(triangle,2);
	f[location_pointer+2].set_friend(temp_friends[1],0);
	f[location_pointer+2].set_friend(triangle,1);
	f[location_pointer+2].set_friend(location_pointer+1,2);
	// _update the friends list of the friends
	if(temp_friends[1]!=last_loc)
	{
		i=find_index(f[temp_friends[1]],triangle);
		f[temp_friends[1]].set_friend(location_pointer+2,i);
	}
	if(temp_friends[0]!=last_loc)
	{
		i=find_index(f[temp_friends[0]],triangle);
		f[temp_friends[0]].set_friend(location_pointer+1,i);
	}
	// Calculate radius if needed
	if(CalcRadius)
	{
		radius[triangle]=CalculateRadius(triangle);
		int n=int(f.size());
		int m=int(radius.size());
		if(n>m-1)
		{
			radius.push_back(CalculateRadius(location_pointer+1));
			radius.push_back(CalculateRadius(location_pointer+2));
		}
		else 
			if(n>m)
			{
				radius[location_pointer+1]=CalculateRadius(location_pointer+1);
				radius.push_back(CalculateRadius(location_pointer+2));
			}
			else
			{
				radius[location_pointer+1]=CalculateRadius(location_pointer+1);
				radius[location_pointer+2]=CalculateRadius(location_pointer+2);
			}
	}

	// check if flipping is needed
	flip(triangle,temp_friends[2]);
	flip(location_pointer+1,temp_friends[0]);
	flip(location_pointer+2,temp_friends[1]);

	// _update number of facets
	location_pointer+=2;
}	

void Delaunay::flip(int i, int j)
{
	stack<boost::array<int,2> > flip_stack;
	boost::array<int,2> array_temp={{i,j}};
	flip_stack.push(array_temp);
	boost::array<int,2> check;
	boost::array<int,2> other;
	boost::array<int,2> indexes;
	boost::array<Vector2D,4> circle_test;
	while(flip_stack.size()>0)
	{
		if(flip_stack.top()[1]==last_loc)
			flip_stack.pop();
		else
		{
			indexes[0]=flip_stack.top()[0];
			indexes[1]=flip_stack.top()[1];
			// Returns the index to the point to check in coordinates and the index of the point in the facet
			find_diff(&f[indexes[1]],&f[indexes[0]],&check[0]); 
			find_diff(&f[indexes[0]],&f[indexes[1]],&other[0]);

			for(int k=0;k<3;++k)
			{
				circle_test[k]=cor[f[indexes[0]].get_vertice(k)];
			}
			circle_test[3]=cor[check[0]];

			if(incircle(circle_test)>0)
			{
				//The point is in a circle change the facets and their friends
				const int v1=f[indexes[0]].get_vertice((other[1]+1)%3);
				const int f1=f[indexes[0]].get_friend(other[1]);
				const int f12=f[indexes[0]].get_friend((other[1]+2)%3);
				const int f13=f[indexes[0]].get_friend((other[1]+2)%3);
				const int v2=f[indexes[1]].get_vertice((check[1]+1)%3);
				const int f2=f[indexes[1]].get_friend((check[1]+2)%3);
				const int f22=f[indexes[1]].get_friend(check[1]);
				const int f23=f[indexes[1]].get_friend((check[1]+2)%3);
				f[indexes[0]].set_vertice(other[0],0);
				f[indexes[0]].set_vertice(v1,1);
				f[indexes[0]].set_vertice(check[0],2);
				f[indexes[1]].set_vertice(check[0],0);
				f[indexes[1]].set_vertice(v2,1);
				f[indexes[1]].set_vertice(other[0],2);
				f[indexes[0]].set_friend(f1,0);
				f[indexes[0]].set_friend(f2,1);
				f[indexes[0]].set_friend(indexes[1],2);
				f[indexes[1]].set_friend(f22,0);
				f[indexes[1]].set_friend(f12,1);
				f[indexes[1]].set_friend(indexes[0],2);
				// change the friends of the friends if needed
				if(f23!=last_loc)
				{
					f[f23].set_friend(indexes[0],find_index(f[f23],indexes[1]));
				}
				if(f13!=last_loc)
				{
					f[f13].set_friend(indexes[1],find_index(f[f13],indexes[0]));
				}
				// Calculate the new radius if needed
				if(CalcRadius)
				{
					radius[indexes[0]]=CalculateRadius(indexes[0]);
					radius[indexes[1]]=CalculateRadius(indexes[1]);
				}
				// clear the checked facets
				flip_stack.pop();
				// push into the stack the new facets to check
				array_temp[0]=indexes[1];
				array_temp[1]=f[indexes[1]].get_friend(0);
				flip_stack.push(array_temp);
				array_temp[0]=indexes[0];
				array_temp[1]=f[indexes[0]].get_friend(1);
				flip_stack.push(array_temp);
			}
			else
			{
				// clear the checked facets
				flip_stack.pop();
			}
		}
	}
}

void Delaunay::build_delaunay(vector<Vector2D>const& vp,vector<Vector2D> const& cpoints)
{
	cell_points=cpoints;
	DataOnlyForBuild data;
	lastFacet=0;
	CalcRadius=false;
	length=int(vp.size()+3);
	int len=length-3;
	olength=len;
	f.clear();
	cor.clear();
	f.reserve(2*length+1+(int)(17*sqrt(1.*length)));
	cor.reserve(length+9*(int)sqrt(1.*length));
	last_loc=INT_MAX;
	for(int i=0;i<len;i++)
	{
		cor.push_back(vp[i]);
	}
	// Check point input
	CheckInput();

	data.insert_order=HilbertOrder(cor,olength,0);

	// add the 3 extreme points
	Vector2D p_temp;
	vector<double> cellsize=CellSize(cell_points);
	double width=cellsize[1]-cellsize[0];
	double height=cellsize[3]-cellsize[2];
	p_temp.set_x(cellsize[0]-100*width);
	p_temp.set_y(cellsize[2]-100*height);
	cor.push_back(p_temp);
	p_temp.set_x(cellsize[1]+100*width);
	p_temp.set_y(cellsize[2]-100*height);
	cor.push_back(p_temp);
	p_temp.set_x((cellsize[0]+cellsize[1])/2.0);
	p_temp.set_y(cellsize[3]+100*height);
	cor.push_back(p_temp);
	// Create the big triangle, and assign friends
	facet f_temp;
	f.push_back(f_temp);
	f[0].set_vertice(len,0);
	f[0].set_vertice(len+1,1);
	f[0].set_vertice(len+2,2);
	for(int i=0;i<3;i++)
		f[0].set_friend(last_loc,i);
	location_pointer=0;
	// add the points
	for(int i=0;i<length-3;i++)
	{
		add_point(data.insert_order[i]);
	}
	// Calculate radius
	radius.resize(f.size());
	int n=int(f.size());
	for(int i=0;i<n;++i)
		radius[i]=CalculateRadius(i);
	CalcRadius=true;
}

void Delaunay::find_diff(facet *f1,facet *f2,int *p) const
{
	bool counter;
	int i=0;
	for(;i<3;++i)
	{
		counter=false;
		for(int j=0;j<3;++j)
		{
			if(f1->get_vertice(i)==f2->get_vertice(j))
			{
				counter=true;
				break;
			}
		}
		if(counter==false)
		{
			p[0]=f1->get_vertice(i);
			p[1]=i;
			return;
		}
	}
	UniversalError eo("Delaunay, Couldn't find difference bewteen two facets");
	eo.AddEntry("Facet 1",double(f1-&f[0]));
	eo.AddEntry("Facet 2",double(f2-&f[0]));
	throw eo;
}

double Delaunay::triangle_area(int index)
{
	boost::array<Vector2D,3> p;
	p[0]=cor[f[index].get_vertice(0)];
	p[1]=cor[f[index].get_vertice(1)];
	p[2]=cor[f[index].get_vertice(2)];
	double x1=p[2].x-p[0].x;
	double x2=p[1].x-p[0].x;
	double y1=p[2].y-p[0].y;
	double y2=p[1].y-p[0].y;
	return -0.5*(x1*y2-x2*y1);
}

void Delaunay::update(const vector<Vector2D>& points,vector<Vector2D>
	const& cpoints)
{
	if(logger)
		logger->output(cor,f);
	build_delaunay(points,cpoints);
}

int Delaunay::Walk(int point)
{ //This method starts from the last added facet and walks to the facet that contains point
	//returns the index of the facet that contains point;
	int cur_facet=lastFacet;
	int finish=0;
	boost::array<Vector2D,3> points;
	points[2]=cor[point];
	while(finish==0)
	{
		finish=1;
		//Test friends
		for(int i=0;i<3;++i)
		{
			points[0]=cor[f[cur_facet].get_vertice(i)];
			points[1]=cor[f[cur_facet].get_vertice((i+1)%3)];
			if(orient2d(points)<0)
			{
				finish=0;
				cur_facet=f[cur_facet].get_friend(i);
				break;
			}
		}
	}
	lastFacet=cur_facet;
	return cur_facet;
}

double Delaunay::FindMaxRadius(int point)
{
	int startFacet=Walk(point);
	vector<int> vec;
	FindContainingTetras(startFacet,point,vec);
	double r=0,temp;
	int n=(int)vec.size();
	for(int i=0;i<n;++i)
	{
		temp=radius[vec[i]];
		if(temp>r)
			r=temp;
	}
	return 2*r;
}

void Delaunay::FindContainingTetras(int StartFacet,int point,vector<int> &result)
{
	int PointLocation=FindPointInFacet(StartFacet,point);
	int NextFacet=f[StartFacet].get_friend(PointLocation);
	result.reserve(8);
	result.push_back(NextFacet);
	while(NextFacet!=StartFacet)
	{
		PointLocation=FindPointInFacet(NextFacet,point);
		NextFacet=f[NextFacet].get_friend(PointLocation);
		result.push_back(NextFacet);
	}
}

int Delaunay::FindPointInFacet(int facet,int point)
{
	for(int i=0;i<3;++i)
		if(f[facet].get_vertice(i)==point)
			return i;
	UniversalError eo("Error in Delaunay, FindPointInFacet");
	eo.AddEntry("Facet number",facet);
	eo.AddEntry("Point number",point);
	throw eo;
}

bool Delaunay::IsOuterFacet(int facet)
{
	//int PointNum=length-1;
	for(int i=0;i<3;++i)
		for(int j=0;j<3;++j)
			if(f[facet].get_vertice(i)==(olength+j))
				return true;
	return false;
}

double Delaunay::CalculateRadius(int facet)
{
	const double big=1e10;
	const double a=cor[f[facet].get_vertice(0)].distance(cor[f[facet].get_vertice(1)]);
	const double b=cor[f[facet].get_vertice(0)].distance(cor[f[facet].get_vertice(2)]);
	const double c=cor[f[facet].get_vertice(2)].distance(cor[f[facet].get_vertice(1)]);
	const double temp1=b+c-a;
	if(temp1<=0)
	{
		if(a>big*b||a>big*c) // Do we have a small edge?
			return 0.5*a;
		else
			return 0.5*(b+c); // we have 3 points on a line
	}
	const double temp2=c+a-b;
	if(temp2<=0)
	{
		if(b>big*a||b>big*c) // Do we have a small edge?
			return 0.5*b;
		else
			return 0.5*(a+c); // we have 3 points on a line
	}
	const double temp3=b-c+a;
	if(temp3<=0)
	{
		if(c>big*b||c>big*a) // Do we have a small edge?
			return 0.5*c;
		else
			return 0.5*(b+a); // we have 3 points on a line
	}
	return a*b*c/sqrt((a+b+c)*temp1*temp2*temp3);
}

void Delaunay::CheckInput()
{
	int n=(int)cor.size();
	for(int i=0;i<n;++i)
	{
		if(!InCell(cell_points,cor[i]))
		{
			UniversalError eo("Mesh point outside cell");
			eo.AddEntry("Point number",i);
			eo.AddEntry("X location",cor[i].x);
			eo.AddEntry("Y location",cor[i].y);
			throw eo;
		}
	}
}

int Delaunay::GetOriginalIndex(int NewPoint) const
{
	return NewPoint;
}

double Delaunay::GetFacetRadius(int facet) const
{
	return radius[facet];
}

void Delaunay::ChangeOlength(int n)
{
	olength=n;
}

void Delaunay::Changelength(int n)
{
	length=n+3;
}

vector<Vector2D>& Delaunay::ChangeCor(void)
{
	return cor;
}

facet* Delaunay::get_facet(int index)
{
	return &f[index];
}

double Delaunay::get_facet_coordinate(int Facet,int vertice, int dim)
{
	if(dim==0) 
		return cor[f[Facet].get_vertice(vertice)].x;
	else 
		return cor[f[Facet].get_vertice(vertice)].y;
}

Vector2D Delaunay::get_point(int index) const
{
	return cor[index];
}

double Delaunay::get_cor(int index, int dim) const
{
	if(dim==0)
		return cor[index].x;
	else if(dim==1)
		return cor[index].y;
	else
		throw UniversalError("Error in Delaunay::get_cor. Invalid index");
}

int Delaunay::get_num_facet(void)
{
	return (int)f.size();
}

int Delaunay::get_length(void) const
{
	return length-3;
}

int Delaunay::get_last_loc(void) const
{
	return last_loc;
}

void Delaunay::set_point(int index, Vector2D p)
{
	cor[index]=p;
}

int Delaunay::GetOriginalLength(void) const
{
	return olength;
}

vector<Vector2D>& Delaunay::GetMeshPoints(void)
{
	return cor;
}

int Delaunay::GetTotalLength(void)
{
	return (int)cor.size();
}

void Delaunay::DoMPIRigid(vector<Vector2D> const& points)
{
	int n=(int)points.size();
//	vector<int> order=HilbertOrder(points,n);
	for(int i=0;i<n;++i)
	{
		cor.push_back(points[i]);
		add_point((int)cor.size()-1);
	}
}

vector<int> Delaunay::DoMPIPeriodic(vector<Vector2D> const& points)
{
	int n=(int)points.size();
	vector<int> order=HilbertOrder(points,n);
	for(int i=0;i<n;++i)
	{
		cor.push_back(points[order[i]]);
		add_point((int)cor.size()-1);
	}
	return order;
}

void Delaunay::AddAditionalPoint(Vector2D const& vec)
{
	cor.push_back(vec);
}

int Delaunay::GetCorSize(void)const
{
	return (int)cor.size();
}