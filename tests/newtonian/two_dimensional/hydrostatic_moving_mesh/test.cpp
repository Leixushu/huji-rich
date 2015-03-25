#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <string>
#include <cstdlib>
#include "source/tessellation/geometry.hpp"
#include "source/newtonian/two_dimensional/hdsim2d.hpp"
#include "source/tessellation/tessellation.hpp"
#include "source/newtonian/common/hllc.hpp"
#include "source/newtonian/common/ideal_gas.hpp"
#include "source/tessellation/VoronoiMesh.hpp"
#include "source/newtonian/two_dimensional/interpolations/pcm2d.hpp"
#include "source/newtonian/two_dimensional/spatial_distributions/uniform2d.hpp"
#include "source/newtonian/two_dimensional/point_motions/eulerian.hpp"
#include "source/newtonian/two_dimensional/point_motions/round_cells.hpp"
#include "source/newtonian/two_dimensional/source_terms/zero_force.hpp"
#include "source/newtonian/two_dimensional/geometric_outer_boundaries/SquareBox.hpp"
#include "source/newtonian/two_dimensional/hydro_boundary_conditions/RigidWallHydro.hpp"
#include "source/newtonian/two_dimensional/diagnostics.hpp"
#include "source/misc/utils.hpp"
#include "source/newtonian/test_2d/main_loop_2d.hpp"
#include "source/misc/mesh_generator.hpp"
#include "source/newtonian/two_dimensional/simple_flux_calculator.hpp"
#include "source/newtonian/two_dimensional/simple_cell_updater.hpp"

using namespace std;
using namespace simulation2d;

namespace {

  vector<ComputationalCell> calc_init_cond(const Tessellation& tess)
  {
    vector<ComputationalCell> res(static_cast<size_t>(tess.GetPointNo()));
    for(size_t i=0;i<res.size();++i){
      res[i].density = 1;
      res[i].pressure = 1;
      res[i].velocity = Vector2D(0,0);
    }
    return res;
  }

  class SimData
  {
  public:

    SimData(void):
      width_(1),
      np_(10),
      init_points_(cartesian_mesh(np_,np_,
				  Vector2D(0,0),
				  Vector2D(width_,width_))),
      outer_(0,width_,width_,0),
      tess_(init_points_,outer_),
      pg_(),
      eos_(5./3.),
      bpm_(),
      rs_(),
      hbc_(rs_),
      point_motion_(bpm_,eos_),
      force_(),
      tsf_(0.3),
      fc_(rs_),
      cu_(),
      sim_(tess_,
	   outer_,
	   pg_,
	   calc_init_cond(tess_),
	   eos_,
	   point_motion_,
	   force_,
	   tsf_,
	   fc_,
	   cu_) {}

    hdsim& getSim(void)
    {
      return sim_;
    }

  private:  
    const double width_;
    const int np_;
    const vector<Vector2D> init_points_;
    const SquareBox outer_;
    VoronoiMesh tess_;
    const SlabSymmetry pg_;
    PCM2D interpm_;
    const IdealGas eos_;
    Eulerian bpm_;
    const Hllc rs_;
    const RigidWallHydro hbc_;
    RoundCells point_motion_;
    ZeroForce force_;
    const SimpleCFL tsf_;
    const SimpleFluxCalculator fc_;
    const SimpleCellUpdater cu_;
    hdsim sim_;
  };

  vector<double> volume_list(hdsim const& sim)
  {
    vector<double> res(sim.getAllCells().size(),0);
    for(size_t i=0;i<res.size();++i)
      res[i] = sim.getCellVolume(i);
    return res;
  }

  class WriteMinMaxVolume: public DiagnosticFunction
  {
  public:

    WriteMinMaxVolume(string const& fname):
      fhandle_(fname.c_str()) {}

    void operator()(hdsim const& sim)
    {
      fhandle_ << min(volume_list(sim)) << " "
	       << max(volume_list(sim)) << endl;
    }

    ~WriteMinMaxVolume(void)
    {
      fhandle_.close();
    }

  private:

    ofstream fhandle_;
  };

  void my_main_loop(hdsim& sim)
  {
    CycleTermination term_cond(100);
    WriteMinMaxVolume diag("res.txt");
    main_loop(sim,
	      term_cond,
	      &hdsim::TimeAdvance,
	      &diag);
  }
}

int main(void)
{
  SimData sim_data;
  hdsim& sim = sim_data.getSim();

  my_main_loop(sim);

  return 0;
}
