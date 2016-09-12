#include "RoundCells3D.hpp"
#ifdef RICH_MPI
#include <mpi.h>
#endif

RoundCells3D::RoundCells3D(const PointMotion3D& pm, const EquationOfState& eos, Vector3D const& ll, Vector3D const& ur,
	double chi,double eta, bool cold) : pm_(pm), eos_(eos), ll_(ll),ur_(ur),chi_(chi), eta_(eta), cold_(cold) {}

namespace
{
	void CorrectPointsOverShoot(vector<Vector3D> &v, double dt, Tessellation3D const& tess,Vector3D const& ll,
		Vector3D const& ur)
	{
		// check that we don't go outside grid
		size_t n = tess.GetPointNo();
		const double inv_dt = 1.0 / dt;
		for (size_t i = 0; i < n; ++i)
		{
			Vector3D point(tess.GetMeshPoint(i));
			double R = tess.GetWidth(i);
			if ((v[i].x*dt * 2 + point.x)>ur.x)
			{
				double factor = 0.25*(ur.x - point.x)*inv_dt / abs(v[i]);
				if (R*0.1 > (ur.x - point.x))
					factor *= -0.1;
				v[i] = v[i] * factor;
			}
			if ((v[i].y*dt * 2 + point.y)>ur.y)
			{
				double factor = 0.25*(ur.y - point.y)*inv_dt / abs(v[i]);
				if (R*0.1 > (ur.y - point.y))
					factor *= -0.1;
				v[i] = v[i] * factor;
			}
			if ((v[i].z*dt * 2 + point.z)>ur.z)
			{
				double factor = 0.25*(ur.z - point.z)*inv_dt / abs(v[i]);
				if (R*0.1 > (ur.z - point.z))
					factor *= -0.1;
				v[i] = v[i] * factor;
			}
			if ((v[i].x*dt * 2 + point.x) < ll.x)
			{
				double factor = 0.25*(point.x - ll.x)*inv_dt / abs(v[i]);
				if (R*0.1 > (point.x - ll.x))
					factor *= -0.1;
				v[i] = v[i] * factor;
			}
			if ((v[i].y*dt * 2 + point.y)<ll.y)
			{
				double factor = 0.25*(point.y - ll.y)*	inv_dt / abs(v[i]);
				if (R*0.1 > (point.y - ll.y))
					factor *= -0.1;
				v[i] = v[i] * factor;
			}
			if ((v[i].z*dt * 2 + point.z)<ll.z)
			{
				double factor = 0.25*(point.z - ll.z)*	inv_dt / abs(v[i]);
				if (R*0.1 > (point.z - ll.z))
					factor *= -0.1;
				v[i] = v[i] * factor;
			}

		}
		return;
	}
}

Vector3D RoundCells3D::calc_dw(size_t i, const Tessellation3D& tess, const vector<ComputationalCell3D>& cells,
	TracerStickerNames const& tracerstickernames) const
{
	const Vector3D r = tess.GetMeshPoint(i);
	const Vector3D s = tess.GetCellCM(i);
	const double d = abs(s - r);
	const double R = tess.GetWidth(i);
	if (d < 0.9*eta_*R)
		return Vector3D(0, 0,0);
	const double c = std::max(eos_.dp2c(cells[i].density, cells[i].pressure,
		cells[i].tracers, tracerstickernames.tracer_names), abs(cells[i].velocity));
	return chi_*c*(s - r) / R;
}

Vector3D RoundCells3D::calc_dw(size_t i, const Tessellation3D& tess, double dt, vector<ComputationalCell3D> const& cells,
	TracerStickerNames const& tracerstickernames)const
{
	const Vector3D r = tess.GetMeshPoint(i);
	const Vector3D s = tess.GetCellCM(i);
	const double d = abs(s - r);
	const double R = tess.GetWidth(i);
	if (d < 0.9*eta_*R)
		return Vector3D(0, 0,0);
	vector<size_t> neigh = tess.GetNeighbors(i);
	size_t N = neigh.size();
	double cs = std::max(abs(cells[i].velocity), eos_.dp2c(cells[i].density, cells[i].pressure,
		cells[i].tracers, tracerstickernames.tracer_names));
	for (size_t j = 0; j < N; ++j)
	{
		cs = std::max(cs, eos_.dp2c(cells[neigh[j]].density, cells[neigh[j]].pressure,
			cells[static_cast<size_t>(neigh[j])].tracers));
		cs = std::max(cs, abs(cells[neigh[j]].velocity));
	}
	const double c_dt = d / dt;
	return chi_*std::min(c_dt, cs)*(s - r) / R;
}

void RoundCells3D::operator()(const Tessellation3D& tess, const vector<ComputationalCell3D>& cells,
	double time, TracerStickerNames const& tracerstickernames, vector<Vector3D> &res) const
{
	pm_(tess, cells, time, tracerstickernames,res);
	if (!cold_)
	{
		size_t n = res.size();
		for (size_t i = 0; i < n; ++i)
			res[i] += calc_dw(i, tess, cells, tracerstickernames);
	}
}

void RoundCells3D::ApplyFix(Tessellation3D const& tess, vector<ComputationalCell3D> const& cells, double time,
	double dt, vector<Vector3D> &velocities, TracerStickerNames const& tracerstickernames)const
{
	pm_.ApplyFix(tess, cells, time, dt, velocities, tracerstickernames);
	velocities.resize(static_cast<size_t>(tess.GetPointNo()));
	if (cold_)
	{
		const size_t n = velocities.size();
		for (size_t i = 0; i < n; ++i)
			velocities.at(i) += calc_dw(i, tess, dt, cells, tracerstickernames);
	}
	CorrectPointsOverShoot(velocities, dt, tess, ll_,ur_);
}