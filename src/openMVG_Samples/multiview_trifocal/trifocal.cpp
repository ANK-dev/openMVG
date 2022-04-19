// Copyright (c) 2019 Pierre MOULON.
//:\file
//\author Pierre MOULON
//\author Gabriel ANDRADE Rio de Janeiro State U.
//\author Ricardo Fabbri, Brown & Rio de Janeiro State U. (rfabbri.github.io) 

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <numeric>

//these are temporary includes, may be removed
// #include <Eigen/StdVector>
#include "openMVG/multiview/projection.hpp"
#include "openMVG/multiview/triangulation.hpp"

#include "minus/minus.h"
#include "minus/chicago-default.h"

/* If you want to compile header-only, include these, so no link is necessary
#include "minus/minus.hxx"
#include "minus/chicago14a-default-data.hxx"
*/

#include "trifocal-util.h"
#include "trifocal.h"



// Mat is Eigen::MatrixXd - matrix of doubles with dynamic size
// Vec3 is Eigen::Vector3d - Matrix< double, 3, 1 >

namespace trifocal3pt {
  
int iteration_global_debug = 0;
unsigned constexpr max_solve_tries = 5; 
using namespace std;
using namespace MiNuS;
using namespace openMVG;

using Mat23 = Eigen::Matrix<double, 2, 3>;

//-------------------------------------------------------------------------------
// Trifocal3PointPositionTangentialSolver
//-------------------------------------------------------------------------------

void Trifocal3PointPositionTangentialSolver::
Solve(
      const Mat &datum_0,
      const Mat &datum_1,
      const Mat &datum_2,
      std::vector<trifocal_model_t> *trifocal_tensor)
{
  double p[io::pp::nviews][io::pp::npoints][io::ncoords2d];
  double tgt[io::pp::nviews][io::pp::npoints][io::ncoords2d]; 
  
  // pack into solver's efficient representation
  for (unsigned ip=0; ip < io::pp::npoints; ++ip) {
      p[0][ip][0] = datum_0(0,ip);
      p[0][ip][1] = datum_0(1,ip);
    tgt[0][ip][0] = datum_0(2,ip); 
    tgt[0][ip][1] = datum_0(3,ip); 
    
      p[1][ip][0] = datum_1(0,ip);
      p[1][ip][1] = datum_1(1,ip);
    tgt[1][ip][0] = datum_1(2,ip); 
    tgt[1][ip][1] = datum_1(3,ip); 
    
      p[2][ip][0] = datum_2(0,ip);
      p[2][ip][1] = datum_2(1,ip);
    tgt[2][ip][0] = datum_2(2,ip); 
    tgt[2][ip][1] = datum_2(3,ip); 
  }
  
  unsigned nsols_final = 0;
  unsigned id_sols[M::nsols];
  double  cameras[M::nsols][io::pp::nviews-1][4][3];  // first camera is always [I | 0]
  for (unsigned i = 0; i < max_solve_tries; ++i) {
    if (MiNuS::minus<chicago>::solve(p, tgt, cameras, id_sols, &nsols_final))
      break;
    std::cerr << "Minus failed once to compute tracks, perhaps retry\n";  // for a good test, this never fails
  }

  // fill C0* with for loop
  // std::cerr << "Number of sols " << nsols_final << std::endl;
  std::vector<trifocal_model_t> &tt = *trifocal_tensor; // if I use the STL container, 
                                                        // This I would have to change the some other pieces of code, maybe altering the entire logic of this program!!
  tt.resize(nsols_final);
  for (unsigned s=0; s < nsols_final; ++s) {
    tt[s][0] = Mat34::Identity(); // view 0 [I | 0]
    for (unsigned v=1; v < io::pp::nviews; ++v) {
        // eigen is col-major but minus is row-major, so memcpy canot be used.
        for (unsigned ir = 0; ir < 3; ++ir)
          for (unsigned ic = 0; ic < 3; ++ic)
            tt[s][v](ir, ic) = cameras[id_sols[s]][v-1][ir][ic];
        for (unsigned r=0; r < 3; ++r)
          tt[s][v](r,3) = cameras[id_sols[s]][v-1][3][r];
    }
  }
  // TODO: filter the solutions by:
  // - positive depth and 
  // - using tangent at 3rd point
  //
  // If we know the rays are perfectly coplanar, we can just use cross
  // product within the plane instead of SVD
}

double Trifocal3PointPositionTangentialSolver::
Error(
  const trifocal_model_t &tt,
  const Vec &bearing_0, // x,y,tangentialx,tangentialy
  const Vec &bearing_1,
  const Vec &bearing_2,
  const Vec &pxbearing_0,
  const Vec &pxbearing_1,
  const Vec &pxbearing_2,
  const double K[2][3] /* for debugging */) 
{
  // Return the cost related to this model and those sample data point
  // Ideal algorithm:
  // 1) reconstruct the 3D points and orientations
  // 2) project the 3D points and orientations on all images_
  // 3) compute error 
  // 
  // In practice we ignore the directions and only reproject to one third view
  // 3x3: each column is x,y,1
  Mat3 bearing;
  bearing << bearing_0.head(2).homogeneous(),
             bearing_1.head(2).homogeneous(), 
             bearing_2.head(2).homogeneous();
  
  // Using triangulation.hpp
  Vec4 triangulated_homg;
  unsigned third_view = 0;
  // pick the wider baseline. TODO: measure all pairwise translation distances
  if (tt[1].col(3).squaredNorm() > tt[2].col(3).squaredNorm()) {
    // TODO(trifocal future) compare to triangulation from the three views at once
    TriangulateDLT(tt[0], bearing.col(0), tt[1], bearing.col(1), &triangulated_homg);
    third_view = 2;
  } else {
    TriangulateDLT(tt[0], bearing.col(0), tt[2], bearing.col(2), &triangulated_homg);
    third_view = 1;
  }
  // std::cerr << "Third view: " << third_view << std::endl;

  // Computing the projection of triangulated points using projection.hpp
  // For prototyping and speed, for now we will only project to the third view
  // and report only one error
  Vec2 p_reprojected = (tt[third_view]*triangulated_homg).hnormalized();
//  std::cerr << "p_reprojected: \n" << p_reprojected << std::endl;
//  std::cerr << "bearing(third_view): \n" << bearing.col(third_view).head(2) << std::endl;
  Vec2 p_reprojected_pix;
  apply_intrinsics(K,p_reprojected_pix.data(),p_reprojected.data());
//  std::cerr << "p_reprojected pix: \n" <<  p_reprojected_pix << std::endl;
//  std::cerr << "bearing(third_view) pix: \n" << pxbearing_1 << std::endl;
  
  return (p_reprojected - bearing.col(third_view).head(2)).squaredNorm();
}

} // namespace trifocal3pt
