// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef OPENMVG_SFM_SFM_FEATURES_PROVIDER_HPP
#define OPENMVG_SFM_SFM_FEATURES_PROVIDER_HPP

#include <memory>
#include <string>

#include "openMVG/features/feature.hpp"
#include "openMVG/features/feature_container.hpp"
#include "openMVG/features/regions.hpp"
#include "openMVG/sfm/sfm_data.hpp"
#include "openMVG/system/logger.hpp"
#include "openMVG/system/loggerprogress.hpp"
#include "openMVG/types.hpp"

#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"

namespace openMVG {
namespace sfm {

/// Abstract PointFeature provider (read some feature and store them as PointFeature).
/// Allow to load and return the features related to a view
struct Features_Provider
{
  /// PointFeature array per ViewId of the considered SfM_Data container
  Hash_Map<IndexT, features::PointFeatures> feats_per_view;

  /// SIOPointFeature array per ViewId of the considered SfM_Data container
  /// This will only be active when feats_per_view is not
  /// This is for backward compatibility with the usual Features_Provider
  /// Ideally, we could have a better structure
  Hash_Map<IndexT, features::SIOPointFeatures> sio_feats_per_view;

  bool has_sio_features() { return !sio_feats_per_view.empty(); }

  virtual ~Features_Provider() = default;

  virtual bool load(
    const SfM_Data & sfm_data,
    const std::string & feat_directory,
    std::unique_ptr<features::Regions>& region_type,
    bool store_as_sio_features = false)
  {
    system::LoggerProgress my_progress_bar(sfm_data.GetViews().size(), "- Features Loading -");
    // Read for each view the corresponding features and store them as PointFeatures
    bool bContinue = true;
#ifdef OPENMVG_USE_OPENMP
    #pragma omp parallel
#endif
    for (Views::const_iterator iter = sfm_data.GetViews().begin();
      iter != sfm_data.GetViews().end() && bContinue; ++iter)
    {
#ifdef OPENMVG_USE_OPENMP
    #pragma omp single nowait
#endif
      {
        const std::string sImageName = stlplus::create_filespec(sfm_data.s_root_path, iter->second->s_Img_path);
        const std::string basename = stlplus::basename_part(sImageName);
        const std::string featFile = stlplus::create_filespec(feat_directory, basename, ".feat");

        std::unique_ptr<features::Regions> regions(region_type->EmptyClone());
        if (!stlplus::file_exists(featFile) || !regions->LoadFeatures(featFile))
        {
          OPENMVG_LOG_ERROR << "Invalid feature files for the view: " << sImageName;
#ifdef OPENMVG_USE_OPENMP
      #pragma omp critical
#endif
          bContinue = false;
        }
#ifdef OPENMVG_USE_OPENMP
      #pragma omp critical
#endif

        auto sift_regions = dynamic_cast<SIFT_Regions *>(regions_type.get());
        if (sift_regions && store_as_sio_features) {
          // save loaded Features as SIOPointFeature for SfM pipeline elements
          // that use feature orientation etc
          sio_feats_per_view[iter->second->id_view] = sift_regions->Features();
        } else {
          // save loaded Features as PointFeature
          feats_per_view[iter->second->id_view] = regions->GetRegionsPositions();
          assert(!store_as_sio_features);
        }
        ++my_progress_bar;
      }
    }
    return bContinue;
  }

  /// Return the PointFeatures belonging to the View, if the view does not exist
  ///  it returns an empty PointFeature array.
  const features::PointFeatures & getFeatures(const IndexT & id_view) const
  {
    // Have an empty feature set in order to deal with non existing view_id
    static const features::PointFeatures emptyFeats = features::PointFeatures();

    Hash_Map<IndexT, features::PointFeatures>::const_iterator it = feats_per_view.find(id_view);
    if (it != feats_per_view.end())
      return it->second;
    else
      return emptyFeats;
  }
}; // Features_Provider

} // namespace sfm
} // namespace openMVG

#endif // OPENMVG_SFM_SFM_FEATURES_PROVIDER_HPP
