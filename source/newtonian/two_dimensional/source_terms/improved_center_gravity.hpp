/*! \file improved_center_gravity.hpp
  \brief Point source gravity force
  \author Elad Steinberg
*/
#ifndef CENTERGRAVITY_HPP
#define CENTERGRAVITY_HPP 1

#include "ConservativeForce.hpp"

//! \brief Point source gravity force
class ImprovedCenterGravity: public Acceleration
{
public:
  /*!
    \brief Class constructor
    \param M The mass of the point source
    \param Rmin The softenning length
    \param center The location of the point source
  */
  ImprovedCenterGravity(double M,double Rmin,const Vector2D& center);

  Vector2D operator()
  (const Tessellation& tess,
   const vector<ComputationalCell>& cells,
   const vector<Extensive>& fluxes,
   const double time,
   const int point) const;

  /*! \brief Returns the position of the center
    \return Position of the center
  */
  Vector2D const& get_center(void) const;

private:
  const double M_;
  const double Rmin_;
  const Vector2D center_;
};

#endif // CENTERGRAVITY_HPP
