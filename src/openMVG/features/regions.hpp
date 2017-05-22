
// Copyright (c) 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef OPENMVG_FEATURES_REGIONS_HPP
#define OPENMVG_FEATURES_REGIONS_HPP

#include "openMVG/types.hpp"
#include "openMVG/numeric/numeric.h"
#include "openMVG/features/feature.hpp"
#include "openMVG/features/descriptor.hpp"
#include "openMVG/matching/metric.hpp"
#include "cereal/types/vector.hpp"
#include <string>
#include <cstddef>
#include <typeinfo>

namespace openMVG {
namespace features {

struct FeatureInImage
{
  FeatureInImage(IndexT featureIndex, IndexT point3dId)
    : _featureIndex(featureIndex)
    , _point3dId(point3dId)
  {}

  IndexT _featureIndex;
  IndexT _point3dId;

  bool operator<(const FeatureInImage& other) const
  {
    return _featureIndex < other._featureIndex;
  }
};


/// Describe an image a set of regions (position, ...) + attributes
/// Each region is described by a set of attributes (descriptor)
class Regions
{
public:

  virtual ~Regions() = 0;

  //--
  // IO - one file for region features, one file for region descriptors
  //--

  virtual bool Load(
    const std::string& sfileNameFeats,
    const std::string& sfileNameDescs) = 0;

  virtual bool Save(
    const std::string& sfileNameFeats,
    const std::string& sfileNameDescs) const = 0;

  virtual bool SaveDesc(const std::string& sfileNameDescs) const = 0;

  virtual bool LoadFeatures(
    const std::string& sfileNameFeats) = 0;

  //--
  //- Basic description of a descriptor [Type, Length]
  //--
  virtual bool IsScalar() const = 0;
  virtual bool IsBinary() const = 0;

  /// basis element used for description
  virtual std::string Type_id() const = 0;
  virtual std::size_t DescriptorLength() const = 0;

  //-- Assume that a region can always be represented at least by a 2D positions
  virtual PointFeatures GetRegionsPositions() const = 0;
  virtual Vec2 GetRegionPosition(std::size_t i) const = 0;

  /// Return the number of defined regions
  virtual std::size_t RegionCount() const = 0;

  /**
   * @brief Return a blind pointer to the container of the descriptors array.
   *
   * @note: Descriptors are always stored as an std::vector<DescType>.
   */
  virtual const void* blindDescriptors() const = 0;

  /**
   * @brief Return a pointer to the first value of the descriptor array.
   *
   * @note: Descriptors are always stored as a flat array of descriptors.
   */
  virtual const void * DescriptorRawData() const = 0;

  virtual void clearDescriptors() = 0;

  /// Return the squared distance between two descriptors
  // A default metric is used according the descriptor type:
  // - Scalar: L2,
  // - Binary: Hamming
  virtual double SquaredDescriptorDistance(std::size_t i, const Regions *, std::size_t j) const = 0;

  /// Add the Inth region to another Region container
  virtual void CopyRegion(std::size_t i, Regions *) const = 0;

  virtual Regions * EmptyClone() const = 0;

  virtual std::unique_ptr<Regions> createFilteredRegions(
                     const std::vector<FeatureInImage>& featuresInImage,
                     std::vector<IndexT>& out_associated3dPoint,
                     std::map<IndexT, IndexT>& out_mapFullToLocal) const = 0;

};

inline Regions::~Regions() {}

template<typename FeatT>
class Feat_Regions : public Regions
{
public:
  /// Region type
  typedef FeatT FeatureT;
  /// Container for multiple regions
  typedef std::vector<FeatureT> FeatsT;

  virtual ~Feat_Regions() {}

protected:
  std::vector<FeatureT> _vec_feats;    // region features

public:
  bool LoadFeatures(const std::string& sfileNameFeats)
  {
    return loadFeatsFromFile(sfileNameFeats, _vec_feats);
  }

  PointFeatures GetRegionsPositions() const
  {
    return PointFeatures(_vec_feats.begin(), _vec_feats.end());
  }

  Vec2 GetRegionPosition(std::size_t i) const
  {
    return Vec2f(_vec_feats[i].coords()).cast<double>();
  }

  /// Return the number of defined regions
  std::size_t RegionCount() const {return _vec_feats.size();}

  /// Mutable and non-mutable FeatureT getters.
  inline std::vector<FeatureT> & Features() { return _vec_feats; }
  inline const std::vector<FeatureT> & Features() const { return _vec_feats; }
};

inline const std::vector<SIOPointFeature>& getSIOPointFeatures(const Regions& regions)
{
  static const std::vector<SIOPointFeature> emptyFeats;

  const Feat_Regions<SIOPointFeature>* sioFeatures = dynamic_cast<const Feat_Regions<SIOPointFeature>*>(&regions);
  if(sioFeatures == nullptr)
    return emptyFeats;
  return sioFeatures->Features();
}


enum class ERegionType: bool
{
  Binary = 0,
  Scalar = 1
};


template<typename T, ERegionType regionType>
struct SquaredMetric;

template<typename T>
struct SquaredMetric<T, ERegionType::Scalar>
{
  using Metric = matching::L2_Vectorized<T>;
};

template<typename T>
struct SquaredMetric<T, ERegionType::Binary>
{
  using Metric = matching::SquaredHamming<T>;
};


template<typename FeatT, typename T, std::size_t L, ERegionType regionType>
class FeatDesc_Regions : public Feat_Regions<FeatT>
{
public:
  typedef FeatDesc_Regions<FeatT, T, L, regionType> This;
  /// Region descriptor
  typedef Descriptor<T, L> DescriptorT;
  /// Container for multiple regions description
  typedef std::vector<DescriptorT> DescsT;

protected:
  std::vector<DescriptorT> _vec_descs; // region descriptions

public:
  std::string Type_id() const {return typeid(T).name();}
  std::size_t DescriptorLength() const {return static_cast<std::size_t>(L);}

  bool IsScalar() const { return regionType == ERegionType::Scalar; }
  bool IsBinary() const { return regionType == ERegionType::Binary; }

  Regions * EmptyClone() const
  {
    return new This();
  }

  /// Read from files the regions and their corresponding descriptors.
  bool Load(
    const std::string& sfileNameFeats,
    const std::string& sfileNameDescs)
  {
    return loadFeatsFromFile(sfileNameFeats, this->_vec_feats)
          & loadDescsFromBinFile(sfileNameDescs, _vec_descs);
  }

  /// Export in two separate files the regions and their corresponding descriptors.
  bool Save(
    const std::string& sfileNameFeats,
    const std::string& sfileNameDescs) const
  {
    return saveFeatsToFile(sfileNameFeats, this->_vec_feats)
          & saveDescsToBinFile(sfileNameDescs, _vec_descs);
  }

  bool SaveDesc(const std::string& sfileNameDescs) const
  {
    return saveDescsToBinFile(sfileNameDescs, _vec_descs);
  }

  /// Mutable and non-mutable DescriptorT getters.
  inline std::vector<DescriptorT> & Descriptors() { return _vec_descs; }
  inline const std::vector<DescriptorT> & Descriptors() const { return _vec_descs; }

  inline const void* blindDescriptors() const override { return &_vec_descs; }

  inline const void* DescriptorRawData() const override { return &_vec_descs[0];}

  inline void clearDescriptors() override { _vec_descs.clear(); }

  inline void swap(This& other)
  {
    this->_vec_feats.swap(other._vec_feats);
    _vec_descs.swap(other._vec_descs);
  }

  // Return the distance between two descriptors
  double SquaredDescriptorDistance(std::size_t i, const Regions * genericRegions, std::size_t j) const
  {
    assert(i < this->_vec_descs.size());
    assert(genericRegions);
    assert(j < genericRegions->RegionCount());

    const This * regionsT = dynamic_cast<const This*>(genericRegions);
    static typename SquaredMetric<T, regionType>::Metric metric;
    return metric(this->_vec_descs[i].getData(), regionsT->_vec_descs[j].getData(), DescriptorT::static_size);
  }

  /**
   * @brief Add the Inth region to another Region container
   * @param[in] i: index of the region to copy
   * @param[out] outRegionContainer: the output region group to add the region
   */
  void CopyRegion(std::size_t i, Regions * outRegionContainer) const
  {
    assert(i < this->_vec_feats.size() && i < this->_vec_descs.size());
    static_cast<This*>(outRegionContainer)->_vec_feats.push_back(this->_vec_feats[i]);
    static_cast<This*>(outRegionContainer)->_vec_descs.push_back(this->_vec_descs[i]);
  }

  /**
   * @brief Duplicate only reconstructed regions.
   *
   * @param genericRegions list of input regions from the image extraction
   * @param featuresInImage list of features with an associated 3D point Id
   * @param out_associated3dPoint
   * @param out_mapFullToLocal
   */
  std::unique_ptr<Regions> createFilteredRegions(
                     const std::vector<FeatureInImage>& featuresInImage,
                     std::vector<IndexT>& out_associated3dPoint,
                     std::map<IndexT, IndexT>& out_mapFullToLocal) const override
  {
    out_associated3dPoint.clear();
    out_mapFullToLocal.clear();

    This* regionsPtr = new This;
    std::unique_ptr<Regions> regions(regionsPtr);
    regionsPtr->Features().reserve(featuresInImage.size());
    regionsPtr->Descriptors().reserve(featuresInImage.size());
    out_associated3dPoint.reserve(featuresInImage.size());
    for(std::size_t i = 0; i < featuresInImage.size(); ++i)
    {
      const FeatureInImage & feat = featuresInImage[i];
      regionsPtr->Features().push_back(this->_vec_feats[feat._featureIndex]);
      regionsPtr->Descriptors().push_back(this->_vec_descs[feat._featureIndex]);

      // This assert should be valid in theory, but in practice we notice that sometimes
      // we have the same point associated to different 3D points (2 in practice).
      // In this particular case, currently it returns randomly the last one...
      //
      // assert(out_mapFullToLocal.count(feat._featureIndex) == 0);

      out_mapFullToLocal[feat._featureIndex] = i;
      out_associated3dPoint.push_back(feat._point3dId);
    }
    return regions;
  }

  template<class Archive>
  void serialize(Archive & ar)
  {
    ar(this->_vec_feats);
    ar(_vec_descs);
  }
};


template<typename FeatT, typename T, std::size_t L>
using Scalar_Regions = FeatDesc_Regions<FeatT, T, L, ERegionType::Scalar>;

template<typename FeatT, std::size_t L>
using Binary_Regions = FeatDesc_Regions<FeatT, unsigned char, L, ERegionType::Binary>;



} // namespace features
} // namespace openMVG

#endif // OPENMVG_FEATURES_REGIONS_HPP
