#define _USE_MATH_DEFINES
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <boost/assign/list_of.hpp>
#include "source/misc/utils.hpp"
#include "source/misc/func_1_var.hpp"
#include "source/tessellation/geometry.hpp"
#include "source/tessellation/VoronoiMesh.hpp"
#include "source/newtonian/two_dimensional/interpolations/pcm2d.hpp"
#include "source/newtonian/two_dimensional/interpolations/linear_gauss.hpp"
#include "source/newtonian/two_dimensional/interpolations/eos_consistent.hpp"
#include "source/newtonian/two_dimensional/hdsim2d.hpp"
#include "source/newtonian/common/ideal_gas.hpp"
#include "source/newtonian/two_dimensional/point_motions/lagrangian.hpp"
#include "source/newtonian/two_dimensional/point_motions/round_cells.hpp"
#include "source/newtonian/two_dimensional/point_motions/eulerian.hpp"
#include "source/newtonian/common/hllc.hpp"
#include "source/newtonian/two_dimensional/hydro_boundary_conditions/RigidWallHydro.hpp"
#include "source/newtonian/two_dimensional/source_terms/zero_force.hpp"
#include "source/newtonian/two_dimensional/geometric_outer_boundaries/SquareBox.hpp"
#include "source/newtonian/two_dimensional/spatial_distributions/uniform2d.hpp"
#include "source/newtonian/test_2d/main_loop_2d.hpp"
#include "source/newtonian/two_dimensional/hdf5_diagnostics.hpp"
#include "source/newtonian/two_dimensional/custom_evolutions/RigidBodyEvolve.hpp"
#include "source/newtonian/two_dimensional/custom_evolutions/ConstantPrimitiveEvolution.hpp"
#include "source/misc/mesh_generator.hpp"
#include "source/newtonian/two_dimensional/source_terms/ConservativeForce.hpp"
#include "source/newtonian/two_dimensional/source_terms/CenterGravity.hpp"
#include "source/newtonian/two_dimensional/source_terms/SeveralSources.hpp"
#include "source/newtonian/two_dimensional/pcm_scalar.hpp"
#include "source/misc/simple_io.hpp"
#include "source/newtonian/two_dimensional/RemovalStrategy.hpp"
#include "source/newtonian/two_dimensional/linear_gauss_scalar.hpp"
#include "source/newtonian/two_dimensional/RefineStrategy.hpp"

using namespace std;
using namespace simulation2d;

class AzimuthalVelocity: public SpatialDistribution
{
public:

  AzimuthalVelocity(Func1Var const& radial,
		    Vector2D const& center,
		    char comp):
    radial_(radial),
    center_(center),
    comp_(comp) {}

  double EvalAt(Vector2D const& p) const
  {
    const Vector2D rvec = p - center_;
    const double radius = abs(rvec);
    const double vq = radial_.eval(radius);
    if(comp_=='x')
      return -vq*rvec.y/radius;
    else if(comp_=='y')
      return vq*rvec.x/radius;
    else
      throw "Unknown component name "+comp_;
  }

private:
  Func1Var const& radial_;
  const Vector2D center_;
  const char comp_;
};

class RadialVelocity: public SpatialDistribution
{
public:

  RadialVelocity(Func1Var const& radial,
		 Vector2D const& center,
		 char comp):
    radial_(radial),
    center_(center),
    comp_(comp) {}

  double EvalAt(Vector2D const& p) const
  {
    const Vector2D rvec = p - center_;
    const double radius = abs(rvec);
    const double vr = radial_.eval(radius);
    if(comp_=='x')
      return vr*rvec.x/radius;
    else if(comp_=='y')
      return vr*rvec.y/radius;
    else
      throw "Unknown component name "+comp_;
  }

private:
  Func1Var const& radial_;
  const Vector2D center_;
  const char comp_;
};

class PowerLaw: public Func1Var
{
public:
  PowerLaw(double prefactor,
	   double power):
    prefactor_(prefactor),
    power_(power) {}

  double eval(double x) const
  {
    return prefactor_*pow(x,power_);
  }

private:

  const double prefactor_;
  const double power_;
};

class RotatedKepler: public Func1Var
{
public:

  RotatedKepler(double donor_mass,
		double acceptor_mass,
		double separation):
    donor_mass_(donor_mass),
    acceptor_mass_(acceptor_mass),
    separation_(separation) {}

  double eval(double r) const
  {
    return r*(sqrt(acceptor_mass_/pow(r,3))-
	      sqrt((acceptor_mass_+donor_mass_)/pow(separation_,3)));
  }

private:
  const double donor_mass_;
  const double acceptor_mass_;
  const double separation_;
};

class EladProfile: public SpatialDistribution
{
public:

  EladProfile(char comp,
	      double acceptor_mass,
	      double donor_mass,
	      double separation,
	      Vector2D const& cm):
    comp_(comp),
    acceptor_mass_(acceptor_mass),
    donor_mass_(donor_mass),
    separation_(separation),
    cm_(cm) {}

  double EvalAt(Vector2D const& pos) const
  {
    const double cm_distance = abs(pos-cm_);
    const double v = sqrt(abs(pos)*acceptor_mass_)-
      cm_distance*sqrt((donor_mass_+acceptor_mass_)/pow(separation_,3));
    if(comp_=='x')
      return -v*pos.y/abs(pos);
    else if(comp_=='y')
      return v*pos.x/abs(pos);
    else
      throw "problem";
  }

private:
  const char comp_;
  const double acceptor_mass_;
  const double donor_mass_;
  const double separation_;
  const Vector2D cm_;
};

class BoundedPowerLaw: public Func1Var
{
public:

  BoundedPowerLaw(double prefactor, 
		  double power,
		  double upper_bound,
		  double lower_bound):
    prefactor_(prefactor),
    power_(power),
    upper_bound_(upper_bound),
    lower_bound_(lower_bound) {}

  double eval(double x) const
  {
    const double res = prefactor_*pow(x,power_);
    if(res>upper_bound_)
      return upper_bound_;
    else if(res<lower_bound_)
      return lower_bound_;
    else
      return res;
  }

private:
  const double prefactor_;
  const double power_;
  const double upper_bound_;
  const double lower_bound_;
};

class MagicNumbers
{
public:

  MagicNumbers(void):
    lower_left(-0.38,-0.38),
    upper_right(0.38,0.38),
    total_point_number(10000),
    min_radius(0.05),
    max_radius(0.36),
    cell_size(sqrt(M_PI*(pow(max_radius,2)-pow(min_radius,2))/
		   total_point_number)),
    nr_inner(int(2*M_PI*min_radius/cell_size)),
    nr_outer(int(2*M_PI*max_radius/cell_size)),
    center(0,0),
    adiabatic_index(5./3.),
    density(1e-5),
    pressure(1e-4/adiabatic_index),
    donor_mass(0.5),
    donor_center(1,0),
    acceptor_mass(0.5),
    center_of_gravity((donor_center*acceptor_mass+
		       donor_mass*center)/
		      (donor_mass+acceptor_mass)),
    angular_velocity(sqrt(donor_mass+acceptor_mass)/
		     pow(abs((donor_center-center)),3)),
    radial_velocity(donor_mass,
		    acceptor_mass,
		    abs(center-donor_center)),
    orifice(0.02),
    wind_pressure(1e-4/adiabatic_index),
    wind_velocity(-1e-2,0),
    wind_density(1) {}

  const Vector2D lower_left;
  const Vector2D upper_right;
  const int total_point_number;
  const double min_radius;
  const double max_radius;
  const double cell_size;
  const int nr_inner;
  const int nr_outer;
  const Vector2D center;
  const double adiabatic_index;
  const double density;
  const double pressure;
  const double donor_mass;
  const Vector2D donor_center;
  const double acceptor_mass;
  const Vector2D center_of_gravity;
  const double angular_velocity;
  const RotatedKepler radial_velocity;
  const double orifice;
  const double wind_pressure;
  const Vector2D wind_velocity;
  const double wind_density;
};

class AlignedRectangle
{
public:

  AlignedRectangle(Vector2D const& lower_left,
		   Vector2D const& upper_right):
    lower_left_(lower_left),
    upper_right_(upper_right) {}

private:
  const Vector2D lower_left_;
  const Vector2D upper_right_;
};

namespace{

  template<class T> vector<T> join(vector<T> const& v1,
				   vector<T> const& v2)
  {
    vector<T> res(v1.size()+v2.size());
    copy(v1.begin(),v1.end(),res.begin());
    copy(v2.begin(),v2.end(),res.begin()+v1.size());
    return res;
  }

  vector<Vector2D> grid_func(MagicNumbers const& mm=MagicNumbers())
  {
    const vector<Vector2D> v1 = Circle(mm.nr_inner,
				       mm.min_radius-0.5*mm.cell_size,
				       mm.center.x,
				       mm.center.y);
    const vector<Vector2D> v2 = Circle(mm.nr_outer,
				       mm.max_radius+0.5*mm.cell_size,
				       mm.center.x,
				       mm.center.y);
    const vector<Vector2D> v3 = Circle(mm.nr_inner,
				       mm.min_radius+0.5*mm.cell_size,
				       mm.center.x,
				       mm.center.y);
    const vector<Vector2D> v4 = Circle(mm.nr_outer,
				       mm.max_radius-0.5*mm.cell_size,
				       mm.center.x,
				       mm.center.y);
    const vector<Vector2D> v5 = CirclePointsRmax(mm.total_point_number,
						 mm.min_radius+mm.cell_size,
						 mm.max_radius-mm.cell_size,
						 mm.center.x,
						 mm.center.y);
    return join(join(join(join(v1,v2),v3),v4),v5);    
  }

  class ConsecutiveSnapshots: public DiagnosticFunction
  {
  public:

    ConsecutiveSnapshots(double dt):
      next_time_(0),
      dt_(dt),
      counter_(0) {}

    void diagnose(hdsim const& sim)
    {
      write_number(sim.GetTime(),"time.txt");

      if(sim.GetTime()>next_time_){
	next_time_ += dt_;

	write_snapshot_to_hdf5(sim,"snapshot_"+int2str(counter_)+".h5");
	++counter_;
      }
    }

  private:
    double next_time_;
    const double dt_;
    int counter_;
  };

namespace{

  vector<double> calc_merit(hdsim const& sim,
			    double outermost,
			    double out_threshold,
			    double in_threshold,
			    double innermost,
			    int start_index)
  {
    Tessellation const* tess = sim.GetTessellation();
    const int np = tess->GetPointNo();
    vector<double> res(np,-1);
    for(int i=start_index;i<np;++i){
      if(!sim.CellsEvolve[i]){
	const Vector2D mp = tess->GetMeshPoint(i);
	if(abs(mp)>=outermost){
	  UniversalError eo("Point escaped from outer radius");
	  eo.AddEntry("point index",i);
	  eo.AddEntry("x coordinate",mp.x);
	  eo.AddEntry("y coordinate",mp.y);
	  throw eo;
	}
	if(abs(mp)<=innermost){
	  UniversalError eo("Point escaped from inner radius");
	  eo.AddEntry("point index",i);
	  eo.AddEntry("x coordinate",mp.x);
	  eo.AddEntry("y coordinate",mp.y);
	  throw eo;  
	}
	if(abs(mp)>out_threshold)
	  res[i] = 1.0/(outermost-abs(mp));
	else if(abs(mp)<in_threshold)
	  res[i] = 1.0/(abs(mp)-innermost);
      }
    }
    return res;
  }

  vector<double> merit_reduce(vector<double> const& merit_list)
  {
    vector<double> res;
    for(int i=0;i<(int)merit_list.size();++i){
      if(merit_list[i]>0)
	res.push_back(merit_list[i]);
    }
    return res;
  }

  vector<int> removal_list(vector<double> const& merit_list)
  {
    vector<int> res;
    for(int i=0;i<(int)merit_list.size();++i){
      if(merit_list[i]>0)
	res.push_back(i);
    }
    return res;
  }
}

  class DiskRemove: public RemovalStrategy
  {
  public:

    DiskRemove(hdsim const& sim,
	       double outermost,
	       double out_threshold,
	       double in_threshold,
	       double innermost,
	       int start_index):
      sim_(sim),
      outermost_(outermost),
      out_threshold_(out_threshold),
      in_threshold_(in_threshold),
      innermost_(innermost),
      start_index_(start_index) {}

    vector<int> CellsToRemove(Tessellation const* tess,
			      vector<Primitive> const& /*cells*/,
			      vector<vector<double> > const& /*tracers*/,
			      double /*time*/) const
    {
      const vector<double> raw_merits = calc_merit(sim_,
						   outermost_,
						   out_threshold_,
						   in_threshold_,
						   innermost_,
						   start_index_);
      const vector<int> candidates = removal_list(raw_merits);
      const vector<double> merits = merit_reduce(raw_merits);
      vector<int> result = RemoveNeighbors(merits,
					   candidates,
					   tess);
      CheckOutput(tess,result);
      return result;
    }

  private:
    hdsim const& sim_;
    const double outermost_;
    const double out_threshold_;
    const double in_threshold_;
    const double innermost_;
    const int start_index_;
  };

  class DiskRefine: public RefineStrategy
  {
  public:

    DiskRefine(double a_min,
	       double d_min,
	       MagicNumbers const& mm = MagicNumbers()):
      a_min_(a_min),
      d_min_(d_min),
      min_radius_(mm.min_radius),
      cell_size_(mm.cell_size),
      special_cell_no_(2*(mm.nr_inner+mm.nr_outer)) {}

    vector<int> CellsToRefine
    (Tessellation const* tess,
     vector<Primitive> const& cells,vector<vector<double> > const& /*tracers*/,
     double /*time*/,vector<Vector2D> & directions ,vector<int> const& Removed)
    {
      vector<int> stage1;
      for(int i=special_cell_no_;i<tess->GetPointNo();++i){
	if(cells[i].Density>d_min_)
	  stage1.push_back(i);
      }
      vector<int> stage2;
      for(int i=0;i<(int)stage1.size();++i){
	if(abs(tess->GetMeshPoint(stage1[i]))>min_radius_+3*cell_size_)
	  stage2.push_back(stage1[i]);
      }
      vector<int> res;
      for(int i=0;i<(int)stage2.size();++i){
	if(tess->GetVolume(stage2[i])>a_min_)
	  res.push_back(stage2[i]);
      }
      return RemoveDuplicatedLately(res,
				    tess->GetPointNo(),
				    directions,
				    Removed);
    }

  private:
    const double a_min_;
    const double d_min_;
    const double min_radius_;
    const double cell_size_;
    const int special_cell_no_;
  };

  void my_main_loop(hdsim& sim)
  {
    const double tf = 10;
    SafeTimeTermination term_cond(tf, 1e6);
    //    WriteTime diag("time.txt");
    ConsecutiveSnapshots diag(tf/1000);
    try{
      /*
      main_loop(sim,
      term_cond,
      1,
      &diag);
      */

      MagicNumbers mm;
      DiskRemove disk_remove
	(sim,
	 mm.max_radius,
	 mm.max_radius-1.25*mm.cell_size,
	 mm.min_radius+1.25*mm.cell_size,
	 mm.min_radius,
	 2*mm.nr_inner+2*mm.nr_outer);
      DiskRefine disk_refine
	(0.8/mm.total_point_number,
	 0.1);
      
      const double endtime=10;
      sim.SetEndTime(endtime);
      while(sim.GetTime()<endtime){
	sim.TimeAdvance2Mid();
	
	const vector<int> removed = sim.RemoveCells(&disk_remove);
	sim.RefineCells(&disk_refine,removed);

	diag.diagnose(sim);
      }
    }
    catch(UniversalError& eo){
      eo.AddEntry("time",sim.GetTime());
      throw;
    }
  }
}

class Centripetal: public Acceleration
{
public:

  Centripetal(double angular_velocity,
	      Vector2D const& center):
    omega_(angular_velocity),
    center_(center) {}

  Vector2D Calculate(Tessellation const* tess,
		     vector<Primitive> const& /*cells*/,
		     int point,
		     vector<Conserved> const& /*fluxes*/,
		     vector<Vector2D> const& /*point_velocity*/,
		     HydroBoundaryConditions const* /*hbc*/,
		     double /*time*/,
		     double /*dt*/)
  {
    const Vector2D rvec(tess->GetCellCM(point) - center_);
    return pow(omega_,2)*rvec;
  }

private:

  const double omega_;
  const Vector2D center_;
};

namespace {
  Vector2D zcross(Vector2D const& v)
  {
    return Vector2D(v.y,-v.x);
  }
}

class Coriolis: public SourceTerm
{
public:

  Coriolis(double angular_velocity):
    omega_(angular_velocity) {}

  Conserved Calculate(Tessellation const* tess,
		      vector<Primitive> const& cells,
		      int point,
		      vector<Conserved> const& /*fluxes*/,
		      vector<Vector2D> const& /*point_velocity*/,
		      HydroBoundaryConditions const* /*hbc*/,
		      vector<vector<double> > const& /*tracers*/,
		      vector<double>& /*dtracer*/,
		      double /*time*/,
		      double /*dt*/)
  {
    const Vector2D velocity = cells[point].Velocity;
    const Vector2D acceleration = -2*omega_*zcross(velocity);
    const double mass = tess->GetVolume(point)*cells[point].Density;
    const Vector2D force = mass*acceleration;
    return Conserved(0,force,0);
  }

private:
  const double omega_;
};

template<class T> vector<T> four_vector(T t1,
					T t2,
					T t3,
					T t4)
{
  vector<T> res;
  res.push_back(t1);
  res.push_back(t2);
  res.push_back(t3);
  res.push_back(t4);
  return res;
}

class EdgeRepellant: public PointMotion
{
public:

  EdgeRepellant(double inner_radius,
		double outer_radius,
		PointMotion& naive,
		MagicNumbers const& mm = MagicNumbers()):
    inner_radius_(inner_radius),
    outer_radius_(outer_radius),
    naive_(naive),
    omega_(sqrt(mm.acceptor_mass/pow(mm.min_radius,3))),
    nr_inner_(mm.nr_inner),
    nr_outer_(mm.nr_outer) {}

  Vector2D CalcVelocity(int index, 
			Tessellation const* tess,
			vector<Primitive> const& cells,
			double time)
  {
    return naive_.CalcVelocity(index,tess,cells,time);
  }

  vector<Vector2D> calcAllVelocities(Tessellation const* tess,
				     vector<Primitive> const& cells,
				     double time)
  {
    vector<Vector2D> result = naive_.calcAllVelocities
      (tess,cells,time);
    for(int i=0;i<nr_inner_;++i)
      result[i] = -omega_*zcross(tess->GetMeshPoint(i));
    for(int i=nr_inner_;i<nr_inner_+nr_outer_;++i)
      result[i] = Vector2D(0,0);
    for(int i=nr_inner_+nr_outer_;i<2*nr_inner_+nr_outer_;++i)
      result[i] = -omega_*zcross(tess->GetMeshPoint(i));
    for(int i=2*nr_inner_+nr_outer_;i<2*nr_inner_+2*nr_outer_;++i)
      result[i] = Vector2D(0,0);
    for(int i=2*nr_inner_+2*nr_outer_;i<(int)result.size();++i){
      const Vector2D mp = tess->GetMeshPoint(i);
      const Vector2D vm = cells[i].Velocity;
      if(ScalarProd(mp,vm)>0&&
	 abs(mp)>outer_radius_){
	const Vector2D rhat = mp/abs(mp);
	const double vr = ScalarProd(rhat,vm);
	result[i] = 0.1*vr*rhat+(vm-vr*rhat);
      }
      else if(ScalarProd(mp,vm)<0&&
	      abs(mp)<inner_radius_){
	const Vector2D rhat = mp/abs(mp);
	const double vr = ScalarProd(rhat,vm);
	result[i] = 0.1*vr*rhat+(vm-vr*rhat);
      }
    }
    return result;
  }

private:
  const double inner_radius_;
  const double outer_radius_;
  PointMotion& naive_;
  const double omega_;
  const int nr_inner_;
  const int nr_outer_;

  EdgeRepellant(EdgeRepellant const& origin);
  EdgeRepellant& operator=(EdgeRepellant const& origin);
};

class Ratchet: public CustomEvolution
{
public:

  enum DIRECTION {in, out};

  Ratchet(DIRECTION dir,
	  vector<Primitive> const& init_cells):
    dir_(dir),
    init_cells_(init_cells) {}

  bool flux_indifferent(void) const
  {
    return true;
  }

  Conserved CalcFlux(Tessellation const* tess,
		     vector<Primitive> const& cells,
		     double /*dt*/,
		     SpatialReconstruction* /*interp*/,
		     Edge const& edge,
		     Vector2D const& face_velocity,
		     RiemannSolver const& rs,
		     int index,
		     HydroBoundaryConditions const* hbc,
		     double /*time*/,
		     vector<vector<double> > const& /*tracers*/)
  {
    int other;
    int my_index;
    if(edge.GetNeighbor(0)==index){
      my_index = 0;
      other = edge.GetNeighbor(1);
    }
    else{
      my_index = 1;
      other = edge.GetNeighbor(0);
    }
    if(hbc->IsGhostCell(other,tess))
      return Conserved();
    const Vector2D p = Parallel(edge);
    const Vector2D n = 
      tess->GetMeshPoint(edge.GetNeighbor(1))
      -tess->GetMeshPoint(edge.GetNeighbor(0));
    const Vector2D outward = 
      tess->GetMeshPoint(edge.GetNeighbor(1-my_index))
      -tess->GetMeshPoint(edge.GetNeighbor(my_index));
    Primitive ghost = cells[other];
    if (((dir_==in)&&(ScalarProd(ghost.Velocity,outward)>0))||
	((dir_==out)&&(ScalarProd(ghost.Velocity,outward)<0)))
      ghost.Velocity = Reflect(cells[other].Velocity, p);
    Primitive left, right;
    if(0==my_index){
      left = ghost;
      right = cells[other];
    }
    else{
      left = cells[other];
      right = ghost;
    }
    return FluxInBulk(n,p,left,right,face_velocity,rs);
  }

  Primitive UpdatePrimitive(vector<Conserved> const& /*intensives*/,
			    EquationOfState const* /*eos*/,
			    vector<Primitive>& /*cells*/,
			    int index)
  {
    return init_cells_[index];
  }    

private:
  const DIRECTION dir_;
  vector<Primitive> const& init_cells_;
};

class ConstantPrimitive: public CustomEvolution
{
public:

  ConstantPrimitive(Primitive const& primitive):
    primitive_(primitive) {}

  bool flux_indifferent(void) const
  {
    return true;
  }

  Conserved CalcFlux(Tessellation const* tess,
		     vector<Primitive> const& cells, 
		     double /*dt*/,
		     SpatialReconstruction* /*interpolation*/,
		     Edge const& edge,
		     Vector2D const& facevelocity,
		     RiemannSolver const& rs,int index,
		     HydroBoundaryConditions const* /*boundaryconditions*/,
		     double /*time*/,
		     vector<vector<double> > const& /*tracers*/)
  {
    const Vector2D normal = 
      tess->GetMeshPoint(edge.GetNeighbor(1))-
      tess->GetMeshPoint(edge.GetNeighbor(0));
    const Vector2D parallel = Parallel(edge);
    Primitive left, right;
    if(edge.GetNeighbor(0)==index){
      left = primitive_;
      right = cells[edge.GetNeighbor(1)];
    }
    else if(edge.GetNeighbor(1)==index){
      left = cells[edge.GetNeighbor(0)];
      right = primitive_;
    }
    else
      throw "Something bad happened in ConstantPrimitive";
    return FluxInBulk(normal,
		      parallel,
		      left,
		      right,
		      facevelocity,
		      rs);
  }

  Primitive UpdatePrimitive
  (vector<Conserved> const& /*conservedintensive*/,
   EquationOfState const* /*eos*/,
   vector<Primitive>& /*cells*/,
   int /*index*/)
  {
    return primitive_;
  }

private:
  const Primitive primitive_;
};

class SimData
{
public:

  SimData(MagicNumbers const& mm = MagicNumbers()):
    tess_(),
    eos_(5./3.),
    pm_naive_(),
    pm_inter_(pm_naive_),
    point_motion_(mm.min_radius+1.5*mm.cell_size,
		  mm.max_radius-1.5*mm.cell_size,
		  pm_inter_),
    rs_(),
    acceptor_gravity_acceleration_
    (mm.acceptor_mass,
     0,mm.center),
    acceptor_gravity_(&acceptor_gravity_acceleration_),
    donor_gravity_acceleration_
    (mm.donor_mass,
     0,mm.donor_center),
    donor_gravity_(&donor_gravity_acceleration_),
    centripetal_acceleration_(mm.angular_velocity,
			      mm.center_of_gravity),
    centripetal_force_(&centripetal_acceleration_),
    coriolis_(mm.angular_velocity),
    force_(four_vector<SourceTerm*>(&donor_gravity_,
				    &acceptor_gravity_,
				    &centripetal_force_,
				    &coriolis_)),
    zero_force_(),
    outer_(mm.lower_left.x,
	   mm.upper_right.y,
	   mm.upper_right.x,
	   mm.lower_left.y),
    hbc_(rs_),
    naive_interp_(outer_,&hbc_,true,false),
    interp_(naive_interp_,eos_),
    sinterp_(&interp_,&hbc_),
    sim_(grid_func(),  
	 &tess_,
	 &interp_,
	 Uniform2D(mm.density),
	 Uniform2D(mm.pressure),
	 AzimuthalVelocity(mm.radial_velocity,
			   mm.center,'x'),
	 AzimuthalVelocity(mm.radial_velocity,
			   mm.center,'y'),
	 eos_,
	 rs_,
	 &point_motion_,
	 &force_,
	 &outer_,
	 &hbc_,
	 false,
	 false),
    outflow_(Ratchet::in,
	     sim_.GetAllCells()),
    inflow_(CalcPrimitive(mm.wind_density,
			  mm.wind_pressure,
			  mm.wind_velocity,
			  eos_))
  {
    pm_inter_.SetColdFlows(&hbc_);
    sim_.SetColdFlows(0.05,0.05);
    sim_.setTracerInterpolation(&sinterp_);

    for(int i=0;i<sim_.GetCellNo();++i){
      const Vector2D pos = sim_.GetMeshPoint(i);
      if(abs(pos)>mm.max_radius){
	if(abs(pos.y)<mm.orifice&&pos.x>0)
	  sim_.CellsEvolve[i]=&inflow_;
	else
	  sim_.CellsEvolve[i]=&outflow_;
      }
      else if(abs(pos)<mm.min_radius)
	sim_.CellsEvolve[i]=&outflow_;
    }
  }
  
  hdsim& getSim(void)
  {
    return sim_;
  }
   
private:
  VoronoiMesh tess_;
  //  PCM2D interp_;
  const IdealGas eos_;
  Lagrangian pm_naive_;
  RoundCells pm_inter_;
  EdgeRepellant point_motion_;
  const Hllc rs_;
  CenterGravity acceptor_gravity_acceleration_;
  ConservativeForce acceptor_gravity_;
  CenterGravity donor_gravity_acceleration_;
  ConservativeForce donor_gravity_;
  Centripetal centripetal_acceleration_;
  ConservativeForce centripetal_force_;
  Coriolis coriolis_;
  SeveralSources force_;
  ZeroForce zero_force_;
  const SquareBox outer_;
  RigidWallHydro hbc_;
  LinearGauss naive_interp_;
  EOSConsistent interp_;
  LinearGaussScalar sinterp_;
  hdsim sim_;
  Ratchet outflow_;
  ConstantPrimitive inflow_;
};

namespace {
  void report_error(UniversalError const& eo,
		    hdsim const& sim)
  {
    cout << eo.GetErrorMessage() << endl;
    for(int i=0;i<(int)eo.GetFields().size();++i){
      cout << eo.GetFields()[i] << " = "
	   << eo.GetValues()[i] << endl;
      if(eo.GetFields()[i]=="cell index"){
	const int cell_index = (int)eo.GetValues()[i];
	cout << "position = " << sim.GetMeshPoint(cell_index).x << " , "
	     << sim.GetMeshPoint(cell_index).y << endl;
      }
    }
  }
}

namespace{
vector<int> calc_twilight_zone(hdsim const& sim)
{
  vector<int> res;
  for(int i=0;i<(int)sim.GetCellNo();++i){
    if(sim.CellsEvolve[i])
      res.push_back(i);
    else{
      const vector<int> neighbors = sim.GetTessellation()->GetNeighbors(i);
      vector<int> real_neighbors;
      for(int j=0;j<(int)neighbors.size();++j){
	if(neighbors[j]>0&&neighbors[j]<(int)sim.CellsEvolve.size())
	  real_neighbors.push_back(neighbors[j]);
      }
      for(int j=0;j<(int)real_neighbors.size();++j){
	if(sim.CellsEvolve[real_neighbors[j]]){
	  res.push_back(i);
	}
      }
    }
  }
  return res;
}
}

int main(void)
{
  SimData sim_data;
  hdsim& sim = sim_data.getSim();

  write_snapshot_to_hdf5(sim,"initial.h5"); 

  try{
    my_main_loop(sim);
  }
  catch(UniversalError const& eo){
    report_error(eo,sim);
    throw;
  }
  
  write_snapshot_to_hdf5(sim,"final.h5");
}