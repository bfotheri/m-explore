/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2015-2016, Jiri Horner.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Jiri Horner nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************/

#include <gtest/gtest.h>
#include <ros/console.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <opencv2/core/utility.hpp>
#include "testing_helpers.h"

#define private public
#include <combine_grids/merging_pipeline.h>

const std::array<const char*, 13> hector_maps = {
    "map00.pgm", "map05.pgm", "map07.pgm", "map09.pgm", "map11.pgm",
    "map16.pgm", "map19.pgm", "map21.pgm", "map22.pgm", "map25.pgm",
    "map27.pgm", "map28.pgm", "map31.pgm",
};

constexpr bool verbose_tests = true;

TEST(MergingPipeline, canStich0Grid)
{
  std::vector<nav_msgs::OccupancyGridConstPtr> maps;
  combine_grids::MergingPipeline merger;
  merger.feed(maps.begin(), maps.end());
  EXPECT_TRUE(merger.estimateTransform());
  EXPECT_EQ(merger.composeGrids(), nullptr);
  EXPECT_EQ(merger.getTransforms().size(), 0);
}

TEST(MergingPipeline, canStich1Grid)
{
  auto maps = loadMaps(hector_maps.begin(), hector_maps.end());
  combine_grids::MergingPipeline merger;
  merger.feed(maps.begin() + 1, maps.begin() + 2);
  merger.estimateTransform();
  auto merged_grid = merger.composeGrids();

  // sanity of merged grid
  ASSERT_TRUE(static_cast<bool>(merged_grid));
  EXPECT_FALSE(merged_grid->data.empty());
  EXPECT_EQ((merged_grid->info.width) * (merged_grid->info.height),
            merged_grid->data.size());
  // merged must be the same with original
  EXPECT_EQ(merged_grid->info.width, maps[1]->info.width);
  EXPECT_EQ(merged_grid->info.height, maps[1]->info.height);
  EXPECT_EQ(merged_grid->data.size(), maps[1]->data.size());
  for (size_t i = 0; i < merged_grid->data.size(); ++i) {
    EXPECT_EQ(merged_grid->data[i], maps[1]->data[i]);
  }
  // check estimated transforms
  auto transforms = merger.getTransforms();
  EXPECT_EQ(transforms.size(), 1);
  tf2::Transform t;
  tf2::fromMsg(transforms[0], t);
  EXPECT_EQ(tf2::Transform::getIdentity(), t);
}

TEST(MergingPipeline, canStich2Grids)
{
  auto maps = loadMaps(hector_maps.begin(), hector_maps.end());
  combine_grids::MergingPipeline merger;
  merger.feed(maps.begin(), maps.begin() + 2);
  merger.estimateTransform();
  auto merged_grid = merger.composeGrids();

  // sanity of merged grid
  ASSERT_TRUE(static_cast<bool>(merged_grid));
  EXPECT_FALSE(merged_grid->data.empty());
  EXPECT_EQ((merged_grid->info.width) * (merged_grid->info.height),
            merged_grid->data.size());
  // transforms
  auto transforms = merger.getTransforms();
  EXPECT_EQ(transforms.size(), 2);
  tf2::Transform t;
  tf2::fromMsg(transforms[0], t);
  EXPECT_EQ(tf2::Transform::getIdentity(), t);

  if (verbose_tests) {
    for (auto& transform : transforms) {
      std::cout << transform << std::endl;
    }
    saveMap("test_canStich2Grids.pgm", merged_grid);
  }
}

TEST(MergingPipeline, transformsRoundTrip)
{
  auto map = loadMap("map00.pgm");
  combine_grids::MergingPipeline merger;
  merger.feed(&map, &map + 1);
  for (size_t i = 0; i < 1000; ++i) {
    auto t = randomTransform();
    auto in_transform = toMsg(t);
    // normalize input quaternion such that w > 0 (q and -q represents the same
    // transformation)
    if (in_transform.rotation.w < 0.) {
      in_transform.rotation.x *= -1.;
      in_transform.rotation.y *= -1.;
      in_transform.rotation.z *= -1.;
      in_transform.rotation.w *= -1.;
    }
    merger.setTransforms(&in_transform, &in_transform + 1);

    auto out_transforms = merger.getTransforms();
    ASSERT_EQ(out_transforms.size(), 1);
    auto out_transform = out_transforms[0];
    EXPECT_FLOAT_EQ(in_transform.translation.x, out_transform.translation.x);
    EXPECT_FLOAT_EQ(in_transform.translation.y, out_transform.translation.y);
    EXPECT_FLOAT_EQ(in_transform.translation.z, out_transform.translation.z);
    EXPECT_FLOAT_EQ(in_transform.rotation.x, out_transform.rotation.x);
    EXPECT_FLOAT_EQ(in_transform.rotation.y, out_transform.rotation.y);
    EXPECT_FLOAT_EQ(in_transform.rotation.z, out_transform.rotation.z);
    EXPECT_FLOAT_EQ(in_transform.rotation.w, out_transform.rotation.w);
  }
}

TEST(MergingPipeline, setTransformsInternal)
{
  auto map = loadMap("map00.pgm");
  combine_grids::MergingPipeline merger;
  merger.feed(&map, &map + 1);

  for (size_t i = 0; i < 1000; ++i) {
    auto transform = randomTransform();
    geometry_msgs::Transform t = toMsg(transform);
    merger.setTransforms(&t, &t + 1);

    ASSERT_EQ(merger.transforms_.size(), 1);
    auto& transform_internal = merger.transforms_[0];
    // verify that transforms are the same in 2D
    tf2::Vector3 a[2] = {{1., 0., 1.}, {0., 1., 1.}};
    cv::Point3d b[2] = {{1., 0., 1.}, {0., 1., 1.}};
    for (auto j : {0, 1}) {
      auto p1 = transform * a[j];
      cv::Mat p2 = transform_internal * cv::Mat(b[j]);
      // some precision is naturally lost during conversion, float precision is
      // still good for us
      EXPECT_FLOAT_EQ(p1.x(), p2.at<double>(0));
      EXPECT_FLOAT_EQ(p1.y(), p2.at<double>(1));
    }
  }
}

TEST(MergingPipeline, getTransformsInternal)
{
  auto map = loadMap("map00.pgm");
  combine_grids::MergingPipeline merger;
  merger.feed(&map, &map + 1);

  // set internal transform
  merger.transforms_.resize(1);
  for (size_t i = 0; i < 1000; ++i) {
    cv::Mat transform_internal = randomTransformMatrix();
    merger.transforms_[0] = transform_internal;
    auto transforms = merger.getTransforms();
    ASSERT_EQ(transforms.size(), 1);
    // output quaternion should be normalized
    auto& q = transforms[0].rotation;
    EXPECT_DOUBLE_EQ(1., q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);

    // verify that transforms are the same in 2D
    tf2::Transform transform;
    fromMsg(transforms[0], transform);
    tf2::Vector3 a[2] = {{1., 0., 1.}, {0., 1., 1.}};
    cv::Point3d b[2] = {{1., 0., 1.}, {0., 1., 1.}};
    for (auto j : {0, 1}) {
      auto p1 = transform * a[j];
      cv::Mat p2 = transform_internal * cv::Mat(b[j]);
      EXPECT_FLOAT_EQ(p1.x(), p2.at<double>(0));
      EXPECT_FLOAT_EQ(p1.y(), p2.at<double>(1));
    }
  }
}

int main(int argc, char** argv)
{
  ros::Time::init();
  if (verbose_tests &&
      ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME,
                                     ros::console::levels::Debug)) {
    ros::console::notifyLoggerLevelsChanged();
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
