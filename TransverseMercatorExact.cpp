/**
 * \file TransverseMercatorExact.cpp
 *
 * Copyright (c) Charles Karney (2008) <charles@karney.com>
 * http://charles.karney.info/geographic
 * and licensed under the LGPL.
 **********************************************************************/

#include "GeographicLib/TransverseMercatorExact.hpp"
#include "GeographicLib/Constants.hpp"
#include <limits>
#include <algorithm>

namespace {
  char RCSID[] = "$Id$";
  char RCSID_H[] = TRANSVERSEMERCATOREXACT_HPP;
}

#if defined(_MSC_VER)
#define hypot _hypot
#endif

namespace GeographicLib {

  const double TransverseMercatorExact::tol =
    std::numeric_limits<double>::epsilon();
  const double TransverseMercatorExact::tol1 = 0.1 * sqrt(tol);
  const double TransverseMercatorExact::tol2 = 0.01 * tol * tol1;
  const double TransverseMercatorExact::taytol = std::pow(tol, 0.6);
      // Overflow value for asinh(tan(pi/2)) etc.
  const double TransverseMercatorExact::ahypover =
    double(std::numeric_limits<double>::digits)
    /log(double(std::numeric_limits<double>::radix)) + 2;

  TransverseMercatorExact::TransverseMercatorExact(double a, double invf,
						   double k0, bool foldp)
    : _a(a)
    , _f(1 / invf)
    , _k0(k0)
    , _mu(_f * (2 - _f))
    , _mv(1 - _mu)
    , _e(sqrt(_mu))
    , _foldp(foldp)
    , _Eu(_mu)
    , _Ev(_mv)
  {}

  const TransverseMercatorExact
  TransverseMercatorExact::UTM(Constants::WGS84_a, Constants::WGS84_invf,
			       Constants::UTM_k0);

  double  TransverseMercatorExact::psi0(double phi) {
    // Rewrite asinh(tan(phi)) = atanh(sin(phi)) which is more accurate.
    // Write tan(phi) this way to ensure that sign(tan(phi)) = sign(phi)
    return asinh(sin(phi) / std::max(cos(phi), 0.1 * tol));
  }

  double  TransverseMercatorExact::psi(double phi) const {
    // Lee 9.4
    return psi0(phi) - _e * atanh(_e * sin(phi));
  }

  double TransverseMercatorExact::psiinv(double psi) const {
    // This is the inverse of psi.  Use Newton's method to solve for q in
    //
    //   psi = q - e * atanh(e * tanh(q))
    //
    // and then substitute phi = atan(sinh(q)).  Note that
    // dpsi/dq = (1 - e^2)/(1 - e^2 * tanh(q)^2)
    double q = psi;		// Initial guess
    for (int i = 0; i < numit; ++i) {
      // Max 3 trips through for double precision
      double
	t = tanh(q),
	dq = -(q - _e * atanh(_e * t) - psi) * (1 - _mu * sq(t)) / _mv;
      q += dq;
      if (std::abs(dq) < tol1)
	break;
    }
    return atan(sinh(q));
  }

  void TransverseMercatorExact::zeta(double u,
				     double snu, double cnu, double dnu,
				     double v,
				     double snv, double cnv, double dnv,
				     double& psi, double& lam) const {
    // Lee 54.17 but write
    // atanh(snu * dnv) = asinh(snu * dnv / sqrt(cnu^2 + _mv * snu^2 * snv^2))
    // atanh(_e * snu / dnv) = asinh(_e * snu / sqrt(_mu * cnu^2 + _mv * cnv^2))
    double
      d1 = sqrt(sq(cnu) + _mv * sq(snu * snv)),
      d2 = sqrt(_mu * sq(cnu) + _mv * sq(cnv));
    psi =
      // Overflow to values s.t. tanh = 1.
      (d1 ? asinh(snu * dnv / d1) : snu < 0 ? -ahypover : ahypover)
      - (d2 ? _e * asinh(_e * snu / d2) : snu < 0 ? -ahypover : ahypover);
    lam = (d1 != 0 && d2 != 0) ?
      atan2(dnu * snv, cnu * cnv) - _e * atan2(_e * cnu * snv, dnu * cnv) : 0;
  }

  void TransverseMercatorExact::dwdzeta(double u,
					double snu, double cnu, double dnu,
					double v,
					double snv, double cnv, double dnv,
					double& du, double& dv) const {
    // Lee 54.21 but write (1 - dnu^2 * snv^2) = (cnv^2 + _mu * snu^2 * snv^2)
    // (see A+S 16.21.4)
    double d = _mv * sq(sq(cnv) + _mu * sq(snu * snv));
    du =  cnu * dnu * dnv * (sq(cnv) - _mu * sq(snu * snv)) / d;
    dv = -snu * snv * cnv * (sq(dnu * dnv) + _mu * sq(cnu)) / d;
  }


  // Starting point for zetainv
  bool TransverseMercatorExact::zetainv0(double psi, double lam,
					  double& u, double& v) const {
    bool retval = false;
    if (psi < -_e * Constants::pi/4 &&
	lam > (1 - 2 * _e) * Constants::pi/2 &&
	psi < lam - (1 - _e) * Constants::pi/2) {
      // N.B. this branch is normally not taken because psi < 0 is converted
      // psi > 0 by Forward.
      //
      // There's a log singularity at w = w0 = Eu.K() + i * Ev.K(),
      // corresponding to the south pole, where we have, approximately
      //
      //   psi = _e + i * pi/2 - _e * atanh(cos(i * (w - w0)/(1 + _mu/2)))
      //
      // Inverting this gives:
      double
	psix = 1 - psi / _e,
	lamx = (Constants::pi/2 - lam) / _e;
      u = asinh(sin(lamx) / hypot(cos(lamx), sinh(psix))) * (1 + _mu/2);
      v = atan2(cos(lamx), sinh(psix)) * (1 + _mu/2);
      u = _Eu.K() - u;
      v = _Ev.K() - v;
    } else if (psi < _e * Constants::pi/2 &&
	       lam > (1 - 2 * _e) * Constants::pi/2) {
      // At w = w0 = i * Ev.K(), we have
      //
      //     zeta = zeta0 = i * (1 - _e) * pi/2
      //     zeta' = zeta'' = 0
      //
      // including the next term in the Taylor series gives:
      //
      // zeta = zeta0 - (_mv * _e) / 3 * (w - w0)^3
      //
      // When inverting this, we map arg(w - w0) = [-90, 0] to
      // arg(zeta - zeta0) = [-90, 180]
      double
	dlam = lam - (1 - _e) * Constants::pi/2,
	rad = hypot(psi, dlam),
	// atan2(dlam-psi, psi+dlam) + 45d gives arg(zeta - zeta0) in range
	// [-135, 225).  Subtracting 180 (since multiplier is negative) makes
	// range [-315, 45).  Multiplying by 1/3 (for cube root) gives range
	// [-105, 15).  In particular the range [-90, 180] in zeta space maps
	// to [-90, 0] in w space as required.
	ang = atan2(dlam-psi, psi+dlam) - 0.75 * Constants::pi;
      // Error using this guess is about 0.21 * (rad/e)^(5/3)
      retval = rad < _e * taytol;
      rad = std::pow(3 / (_mv * _e) * rad, 1/3.0);
      ang /= 3;
      u = rad * cos(ang);
      v = rad * sin(ang) + _Ev.K();
    } else {
      // Use spherical TM, Lee 12.6 -- writing atanh(sin(lam) / cosh(psi)) =
      // asinh(sin(lam) / hypot(cos(lam), sinh(psi))).  This takes care of the
      // log singularity at zeta = Eu.K() (corresponding to the north pole)
      v = asinh(sin(lam) / hypot(cos(lam), sinh(psi)));
      u = atan2(sinh(psi), cos(lam));
      // But scale to put 90,0 on the right place
      u *= _Eu.K() / (Constants::pi/2);
      v *= _Eu.K() / (Constants::pi/2);
    }
    return retval;
  }

  // Invert zeta using Newton's method
  void  TransverseMercatorExact::zetainv(double psi, double lam,
					 double& u, double& v) const {
    if (zetainv0(psi, lam, u, v))
      return;
    // Min iterations = 2, max iterations = 4; mean = 3.1
    for (int i = 0; i < numit; ++i) {
      double snu, cnu, dnu, snv, cnv, dnv;
      _Eu.sncndn(u, snu, cnu, dnu);
      _Ev.sncndn(v, snv, cnv, dnv);
      double psi1, lam1, du1, dv1;
      zeta(u, snu, cnu, dnu, v, snv, cnv, dnv, psi1, lam1);
      dwdzeta(u, snu, cnu, dnu, v, snv, cnv, dnv, du1, dv1);
      psi1 -= psi;
      lam1 -= lam;
      double
	delu = psi1 * du1 - lam1 * dv1,
	delv = psi1 * dv1 + lam1 * du1;
      u -= delu;
      v -= delv;
      double delw2 = sq(delu) + sq(delv);
      if (delw2 < tol2)
	break;
    }
  }

  void TransverseMercatorExact::sigma(double u,
				      double snu, double cnu, double dnu,
				      double v,
				      double snv, double cnv, double dnv,
				      double& xi, double& eta) const {
    // Lee 55.4 writing
    // dnu^2 + dnv^2 - 1 = _mu * cnu^2 + _mv * cnv^2
    double d = _mu * sq(cnu) + _mv * sq(cnv);
    xi = _Eu.E(snu, cnu, dnu) - _mu * snu * cnu * dnu / d;
    eta = v - _Ev.E(snv, cnv, dnv) + _mv * snv * cnv * dnv / d;
  }

  void TransverseMercatorExact::dwdsigma(double u,
					 double snu, double cnu, double dnu,
					 double v,
					 double snv, double cnv, double dnv,
					 double& du, double& dv) const {
    // Reciprocal of 55.9: dw/ds = dn(w)^2/_mv, expanding complex dn(w) using
    // A+S 16.21.4
    double d = _mv * sq(sq(cnv) + _mu * sq(snu * snv));
    double
      dnr = dnu * cnv * dnv,
      dni = - _mu * snu * cnu * snv;
    du = (sq(dnr) - sq(dni)) / d;
    dv = 2 * dnr * dni / d;
  }

  // Starting point for sigmainv
  bool  TransverseMercatorExact::sigmainv0(double xi, double eta,
					   double& u, double& v) const {
    bool retval = false;
    if (eta > 1.25 * _Ev.KE() ||
	(xi < -0.25 * _Eu.E() && xi < eta - _Ev.KE())) {
      // sigma as a simple pole at w = w0 = Eu.K() + i * Ev.K() and sigma is
      // approximated by
      // 
      // sigma = (Eu.E() + i * Ev.KE()) + 1/(w - w0)
      double
	x = xi - _Eu.E(),
	y = eta - _Ev.KE(),
	r2 = sq(x) + sq(y);
      u = _Eu.K() + x/r2;
      v = _Ev.K() - y/r2;      
    } else if ((eta > 0.75 * _Ev.KE() && xi < 0.25 * _Eu.E())
	       || eta > _Ev.KE()) {
      // At w = w0 = i * Ev.K(), we have
      //
      //     sigma = sigma0 = i * Ev.KE()
      //     sigma' = sigma'' = 0
      //
      // including the next term in the Taylor series gives:
      //
      // sigma = sigma0 - _mv / 3 * (w - w0)^3
      //
      // When inverting this, we map arg(w - w0) = [-pi/2, -pi/6] to
      // arg(sigma - sigma0) = [-pi/2, pi/2]
      // mapping arg = [-pi/2, -pi/6] to [-pi/2, pi/2]
      double
	deta = eta - _Ev.KE(),
	rad = hypot(xi, deta),
	// Map the range [-90, 180] in sigma space to [-90, 0] in w space.  See
	// discussion in zetainv0 on the cut for ang.
	ang = atan2(deta-xi, xi+deta) - 0.75 * Constants::pi;
      // Error using this guess is about 0.068 * rad^(5/3)
      retval = rad < 2 * taytol;
      rad = std::pow(3 / _mv * rad, 1/3.0);
      ang /= 3;
      u = rad * cos(ang);
      v = rad * sin(ang) + _Ev.K();
    } else {
      // Else use w = sigma * Eu.K/Eu.E (which is correct in the limit _e -> 0)
      u = xi * _Eu.K()/_Eu.E();
      v = eta * _Eu.K()/_Eu.E();
    }
    return retval;
  }

  // Invert sigma using Newton's method
  void  TransverseMercatorExact::sigmainv(double xi, double eta,
					  double& u, double& v) const {
    if (sigmainv0(xi, eta, u, v))
      return;

    // Min iterations = 2, max iterations = 6; mean = 2.7
    for (int i = 0; i < numit; ++i) {
      double snu, cnu, dnu, snv, cnv, dnv;
      _Eu.sncndn(u, snu, cnu, dnu);
      _Ev.sncndn(v, snv, cnv, dnv);
      double xi1, eta1, du1, dv1;
      sigma(u, snu, cnu, dnu, v, snv, cnv, dnv, xi1, eta1);
      dwdsigma(u, snu, cnu, dnu, v, snv, cnv, dnv, du1, dv1);
      xi1 -= xi;
      eta1 -= eta;
      double
	delu = xi1 * du1 - eta1 * dv1,
	delv = xi1 * dv1 + eta1 * du1;
      u -= delu;
      v -= delv;
      double delw2 = sq(delu) + sq(delv);
      if (delw2 < tol2)
	break;
    }
  }
  

  void TransverseMercatorExact::Scale(double phi, double lam,
				      double snu, double cnu, double dnu,
				      double snv, double cnv, double dnv,
				      double& gamma, double& k) const {
    double c = cos(phi);
    if (c > tol1) {
      // Lee 55.12 -- negated for our sign convention.  gamma gives the bearing
      // (clockwise from true north) of grid north
      gamma = atan2(_mv * snu * snv * cnv, cnu * dnu * dnv);
      // Lee 55.13 with nu given by Lee 9.1 -- in sqrt change the numerator
      // from
      //
      //    (1 - snu^2 * dnv^2) to (_mv * snv^2 + cnu^2 * dnv^2)
      //
      // to maintain accuracy near phi = 90 and change the denomintor from
      //
      //    (dnu^2 + dnv^2 - 1) to (_mu * cnu^2 + _mv * cnv^2)
      //
      // to maintain accuracy near phi = 0, lam = 90 * (1 - e).  Similarly
      // rewrite sqrt term in 9.1 as
      //
      //    _mv + _mu * c^2 instead of 1 - _mu * sin(phi)^2
      //
      // Finally replace 1/cos(phi) by cosh(psi0(phi)) which, near phi = pi/2,
      // behaves in a manner consistent with the last sqrt term.  (At least
      // that's the idea.)
      k = sqrt(_mv + _mu * sq(c)) * cosh(psi0(phi)) *
	sqrt( (_mv * sq(snv) + sq(cnu * dnv)) /
	      (_mu * sq(cnu) + _mv * sq(cnv)) );
    } else {
      // Near the pole
      gamma = lam;
      k = 1;
    }
  }

  void TransverseMercatorExact::Forward(double lon0, double lat, double lon,
					double& x, double& y,
					double& gamma, double& k) const {
    // Avoid losing a bit of accuracy in lon (assuming lon0 is an integer)
    if (lon - lon0 > 180)
      lon -= lon0 - 360;
    else if (lon - lon0 <= -180)
      lon -= lon0 + 360;
    else
      lon -= lon0;
    // Now lon in (-180, 180]
    // Explicitly enforce the parity
    int
      latsign = _foldp && lat < 0 ? -1 : 1,
      lonsign = _foldp && lon < 0 ? -1 : 1;
    lon *= lonsign;
    lat *= latsign;
    bool backside = _foldp && lon > 90;
    if (backside) {
      if (lat == 0)
	latsign = -1;
      lon = 180 - lon;
    }
    double
      phi = lat * Constants::degree,
      lam = lon * Constants::degree;

    // u,v = coordinates for the Thompson TM, Lee 54
    double u, v;
    if (lat == 90) {
      u = _Eu.K();
      v = 0;
    } else if (lat == 0 && lon == 90 * (1 - _e)) {
      u = 0;
      v = _Ev.K();
    } else
      zetainv(psi(phi), lam, u, v);

    double snu, cnu, dnu, snv, cnv, dnv;
    _Eu.sncndn(u, snu, cnu, dnu);
    _Ev.sncndn(v, snv, cnv, dnv);

    double xi, eta;
    sigma(u, snu, cnu, dnu, v, snv, cnv, dnv, xi, eta);
    if (backside)
      xi = 2 * _Eu.E() - xi;
    y = xi * _a * _k0 * latsign;
    x = eta * _a * _k0 * lonsign;

    Scale(phi, lam, snu, cnu, dnu, snv, cnv, dnv, gamma, k);
    gamma /= Constants::degree;
    if (backside)
      gamma = 180 - gamma;
    gamma *= latsign * lonsign;
    k *= _k0;
  }

  void TransverseMercatorExact::Reverse(double lon0, double x, double y,
					double& lat, double& lon,
					double& gamma, double& k) const {
    // This undoes the steps in Forward.
    double
      xi = y / (_a * _k0),
      eta = x / (_a * _k0);
    // Explicitly enforce the parity
    int
      latsign = _foldp && y < 0 ? -1 : 1,
      lonsign = _foldp && x < 0 ? -1 : 1;
    xi *= latsign;
    eta *= lonsign;
    bool backside = _foldp && xi > _Eu.E();
    if (backside)
      xi = 2 * _Eu.E()- xi;

    // u,v = coordinates for the Thompson TM, Lee 54
    double u, v;
    if (xi == 0 && eta == _Ev.KE()) {
      u = 0;
      v = _Ev.K();
    } else
      sigmainv(xi, eta, u, v);

    double snu, cnu, dnu, snv, cnv, dnv;
    _Eu.sncndn(u, snu, cnu, dnu);
    _Ev.sncndn(v, snv, cnv, dnv);
    double phi, lam;
    if (v != 0 || u != _Eu.K()) {
      double psi;
      zeta(u, snu, cnu, dnu, v, snv, cnv, dnv, psi, lam);
      phi = psiinv(psi);
      lat = phi / Constants::degree;
      lon = lam / Constants::degree;
    } else {
      phi = Constants::pi/2;
      lat = 90;
      lon = lam = 0;
    }
    Scale(phi, lam, snu, cnu, dnu, snv, cnv, dnv, gamma, k);
    gamma /= Constants::degree;
    if (backside)
      lon = 180 - lon;
    lon *= lonsign;
    // Avoid losing a bit of accuracy in lon (assuming lon0 is an integer)
    if (lon + lon0 >= 180)
      lon += lon0 - 360;
    else if (lon + lon0 < -180)
      lon += lon0 + 360;
    else
      lon += lon0;
    lat *= latsign;
    if (backside)
      y = 2 * _Eu.E() - y;
    y *= _a * _k0 * latsign;
    x *= _a * _k0 * lonsign;
    if (backside)
      gamma = 180 - gamma;
    gamma *= latsign * lonsign;
    k *= _k0;
  }

} // namespace GeographicLib
