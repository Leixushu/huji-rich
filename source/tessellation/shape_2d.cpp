#include "shape_2d.hpp"

Shape2D::~Shape2D(void) {}

Circle::Circle(Vector2D const& center,
	       double radius):
  center_(center),
  radius_(radius) {}

bool Circle::is_in(Vector2D const& r) const
{
  return abs(r-center_)<radius_;
}

Outside::Outside(Shape2D const& shape):
  shape_(shape) {}

bool Outside::is_in(Vector2D const& r) const
{
  return !shape_.is_in(r);
}
