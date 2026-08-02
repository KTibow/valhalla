#include "gurka.h"

#include <gtest/gtest.h>

using namespace valhalla;
using namespace valhalla::baldr;

// An untagged pedestrian crossing between two cycleways is rideable at walking pace, so the
// bicycle network is not severed at the road crossing.
TEST(CrossingBicycleAccess, UntaggedCrossingConnects) {
  const std::string ascii_map = R"(
      A----B--C----D
  )";
  const gurka::ways ways = {
      {"AB", {{"highway", "cycleway"}}},
      {"BC", {{"highway", "footway"}, {"footway", "crossing"}}},
      {"CD", {{"highway", "cycleway"}}},
  };
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 100);
  auto map = gurka::buildtiles(layout, ways, {}, {}, "test/data/crossing_bicycle_access");

  GraphReader reader(map.config.get_child("mjolnir"));
  const DirectedEdge* crossing = std::get<1>(gurka::findEdge(reader, map.nodes, "BC", "C"));
  ASSERT_NE(crossing, nullptr);
  EXPECT_EQ(crossing->use(), Use::kPedestrianCrossing);
  EXPECT_TRUE(crossing->forwardaccess() & kBicycleAccess);
  EXPECT_TRUE(crossing->reverseaccess() & kBicycleAccess);
  EXPECT_TRUE(crossing->dismount());

  auto result = gurka::do_action(valhalla::Options::route, map, {"A", "D"}, "bicycle");
  gurka::assert::raw::expect_path(result, {"AB", "BC", "CD"});
}

// An explicit bicycle=no on the crossing wins over the default.
TEST(CrossingBicycleAccess, ExplicitBicycleNoWins) {
  const std::string ascii_map = R"(
      A----B--C----D
  )";
  const gurka::ways ways = {
      {"AB", {{"highway", "cycleway"}}},
      {"BC", {{"highway", "footway"}, {"footway", "crossing"}, {"bicycle", "no"}}},
      {"CD", {{"highway", "cycleway"}}},
  };
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 100);
  auto map = gurka::buildtiles(layout, ways, {}, {}, "test/data/crossing_bicycle_no");

  GraphReader reader(map.config.get_child("mjolnir"));
  const DirectedEdge* crossing = std::get<1>(gurka::findEdge(reader, map.nodes, "BC", "C"));
  ASSERT_NE(crossing, nullptr);
  EXPECT_FALSE(crossing->forwardaccess() & kBicycleAccess);

  EXPECT_THROW(gurka::do_action(valhalla::Options::route, map, {"A", "D"}, "bicycle"),
               std::exception);
}

// The crossing default must not leak onto other footway subtypes: an untagged sidewalk between
// the same cycleways stays inaccessible to bicycles.
TEST(CrossingBicycleAccess, SidewalksStayInaccessible) {
  const std::string ascii_map = R"(
      A----B--C----D
  )";
  const gurka::ways ways = {
      {"AB", {{"highway", "cycleway"}}},
      {"BC", {{"highway", "footway"}, {"footway", "sidewalk"}}},
      {"CD", {{"highway", "cycleway"}}},
  };
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 100);
  auto map = gurka::buildtiles(layout, ways, {}, {}, "test/data/crossing_sidewalk_no_leak");

  GraphReader reader(map.config.get_child("mjolnir"));
  const DirectedEdge* sidewalk = std::get<1>(gurka::findEdge(reader, map.nodes, "BC", "C"));
  ASSERT_NE(sidewalk, nullptr);
  EXPECT_EQ(sidewalk->use(), Use::kSidewalk);
  EXPECT_FALSE(sidewalk->forwardaccess() & kBicycleAccess);

  EXPECT_THROW(gurka::do_action(valhalla::Options::route, map, {"A", "D"}, "bicycle"),
               std::exception);
}
