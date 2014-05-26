#include <cmath>
#include <cstddef>
#include "round_cells.hpp"

RoundCells::RoundCells(PointMotion& pm,
	double chi, 
	double eta,
	int innerNum,
	OuterBoundary const* outer):
pm_(pm), 
	chi_(chi), 
	eta_(eta),
	inner_(innerNum),
	outer_(outer),
	coldflows_(false),
	hbc_(0),
	lastdt_(0),
	evencall_(false),
	external_dt_(-1){}

namespace {
	/*
	Vector2D calc_edge_normal(int edge_index,
	Tessellation const* tess)
	{
	Vector2D res = tess->GetEdge(edge_index).GetVertex(1)-
	tess->GetEdge(edge_index).GetVertex(0);
	return res/abs(res);
	}
	*/

	void FixRefinedCells(vector<Vector2D> &vel,Tessellation const* tess,
		HydroBoundaryConditions const* hbc)
	{
		for(size_t i=0;(int)i<tess->GetPointNo();++i)
		{
			const vector<int> edge_index(tess->GetCellEdges((int)i));
			double R=tess->GetWidth((int)i);
			Vector2D r=tess->GetMeshPoint((int)i);
			for(size_t j=0;j<edge_index.size();++j)
			{
				if(DistanceToEdge(r,tess->GetEdge(edge_index[j]))<0.05*R)
				{
					if((int)i==tess->GetEdge(edge_index[j]).GetNeighbor(0))
					{
						if(tess->GetEdge(edge_index[j]).GetNeighbor(1)==-1)
							continue;
						Vector2D p = Parallel(tess->GetEdge(edge_index[j]));
						p=p/abs(p);
						Vector2D n = r - tess->GetEdge(edge_index[j]).GetVertex(0);
						n-=ScalarProd(n,p)*p;
						n=n/abs(n);
						double v_avg=0.5*(ScalarProd(vel[i],p)+ScalarProd(vel[tess->GetEdge(
							edge_index[j]).GetNeighbor(1)],p));
						vel[i]=ScalarProd(vel[i],n)*n+v_avg*p;
						if(!hbc->IsGhostCell(tess->GetEdge(edge_index[j]).GetNeighbor(1),tess))
							vel[tess->GetEdge(edge_index[j]).GetNeighbor(1)]=ScalarProd(
							vel[tess->GetEdge(edge_index[j]).GetNeighbor(1)],n)*n+v_avg*p;
						continue;
					}
				}
			}
		}
	}

	Vector2D calc_dw(int index,
		Tessellation const* tess,
		double c,
		double /*v*/,
		double eta,
		double chi)
	{
		const double R = tess->GetWidth(index);
		const Vector2D s = tess->GetCellCM(index);
		const Vector2D r = tess->GetMeshPoint(index);
		const double d = abs(s-r);

		if(d<(0.9*eta*R))
			return Vector2D();
		else
		{
			if((1.1*eta*R)<=d)
			{
				return chi*c*(s-r)/d;
			}
			else
				return chi*c*(s-r)/d*(d-0.9*eta*R)/(0.2*eta*R);
		}
	}
}

Vector2D RoundCells::CalcVelocity(int index,Tessellation const* tessellation,
	vector<Primitive> const& primitives,double time)
{
	if(index<inner_)
		return Vector2D(0,0);
	else
	{    
		const Vector2D res = pm_.CalcVelocity
			(index, tessellation, primitives,time);
		const Vector2D dw = calc_dw(index,tessellation,primitives[index].SoundSpeed,
			abs(primitives[index].Velocity),eta_,chi_);
		return res + dw;
	}
}

void RoundCells::SetColdFlows(HydroBoundaryConditions *hbc)
{
	hbc_=hbc;
	coldflows_=true;
}

namespace {

	double numeric_velocity_scale(Tessellation const* tess,int index,double dt)
	{
		const Vector2D s = tess->GetCellCM(index);
		const Vector2D r = tess->GetMeshPoint(index);
		return abs(s-r)/dt;
	}
}

vector<Vector2D> RoundCells::calcAllVelocities
	(Tessellation const* tess,
	vector<Primitive> const& cells,double time)
{
	vector<Vector2D> res;
	const int n=(int)cells.size();
	res.reserve(n);
	for(int i=0;i<n;++i)
		res.push_back(CalcVelocity(i,tess,cells,time));
	FixRefinedCells(res,tess,hbc_);
	if(!coldflows_&&outer_==0)
		return res;
	const vector<Vector2D> edge_vel=tess->calc_edge_velocities(hbc_,res,time);
	double dt=CalcTimeStep(tess,cells,edge_vel,hbc_,time);
	if(!evencall_)
	{
		lastdt_=dt;
		evencall_=true;
	}
	else
	{
		evencall_=false;
		if(dt<lastdt_)
			dt=lastdt_;
	}
	if(outer_!=0&&!coldflows_)
	{
		CorrectPointsOverShoot(res,dt,tess);
		return res;
	}
	if(coldflows_)
	{
		if(external_dt_>0)
			dt=min(dt,external_dt_);
		for(int i=0;i<n;++i)
		{
			if(i<inner_)
			{
				res[i].Set(0,0);
			}
			else
			{
				res[i]=pm_.CalcVelocity(i,tess,cells,time);
				const double nvs = numeric_velocity_scale(tess,i,dt);
				const Vector2D dw = calc_dw(i,tess,nvs,nvs,eta_,chi_);   
				res[i]=res[i]+dw; 
			}
		}
		FixRefinedCells(res,tess,hbc_);
		if(outer_!=0)
			CorrectPointsOverShoot(res,dt,tess);
	}
	return res;
}

namespace {
	void LimitNeighborVelocity(vector<Vector2D> &vel,Tessellation const* tess,
		int index,double factor)
	{
		vector<int> neigh=tess->GetNeighbors(index);
		Vector2D r=tess->GetMeshPoint(index);
		double R=tess->GetWidth(index);
		for(size_t i=0;i<neigh.size();++i)
		{
			if(neigh[i]>-1)
			{
				if(r.distance(tess->GetMeshPoint(neigh[i]))<0.1*R)
				{
					vel[neigh[i]]=vel[neigh[i]]*factor;
					return;
				}
			}
		}
	}
}

void RoundCells::CorrectPointsOverShoot(vector<Vector2D> &v,double dt,
	Tessellation const* tess) const
{
	// check that we don't go outside grid
	int n=(int)v.size();
	const double inv_dt=1.0/dt;
	for(int i=inner_;i<n;++i)
	{
		Vector2D point(tess->GetMeshPoint(i));
		if((v[i].x*dt*2+point.x)>outer_->GetGridBoundary(Right))
		{
			double factor=0.9*(outer_->GetGridBoundary(Right)-
				point.x)*inv_dt/abs(v[i]);
			v[i]=v[i]*factor;
			LimitNeighborVelocity(v,tess,i,factor);
		}
		if((v[i].x*dt*2+point.x)<outer_->GetGridBoundary(Left))
		{
			double factor=0.9*(point.x-
				outer_->GetGridBoundary(Left))*inv_dt/abs(v[i]);
			v[i]=v[i]*factor;
			LimitNeighborVelocity(v,tess,i,factor);
		}
		if((v[i].y*dt*2+point.y)>outer_->GetGridBoundary(Up))
		{
			double factor=0.9*(outer_->GetGridBoundary(Up)-point.y)*
				inv_dt/abs(v[i]);
			v[i]=v[i]*factor;
			LimitNeighborVelocity(v,tess,i,factor);
		}
		if((v[i].y*dt*2+point.y)<outer_->GetGridBoundary(Down))
		{
			double factor=0.9*(point.y-outer_->GetGridBoundary(Down))*
				inv_dt/abs(v[i]);
			v[i]=v[i]*factor;
			LimitNeighborVelocity(v,tess,i,factor);
		}
	}
	return;
}

void RoundCells::SetExternalTimeStep(double dt)
{
	external_dt_=dt;
}