#include "gurka.h"
#include "mjolnir/adminbuilder.h"
#include "mjolnir/util.h"
#include "test.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace valhalla;
using namespace valhalla::baldr;

// The direct route A-B-C over a sidewalk and a crossing is 1600m, the plain footway detour
// A-D-E-C is 2000m, and every way is paved, bike-legal and split so no single edge connects
// origin and destination. The shorter direct route wins unless the sidewalk and crossing are
// penalized as service roads (RoadClass::kServiceOther), which prices the detour cheaper.
TEST(BicycleCost, SidewalkAndCrossingNotCostedAsServiceRoad) {
  const std::string ascii_map = R"(
      A-------B-------C
      |               |
      D---------------E
  )";
  const gurka::ways ways = {
      {"AB",
       {{"highway", "footway"}, {"footway", "sidewalk"}, {"bicycle", "yes"}, {"surface", "paved"}}},
      {"BC",
       {{"highway", "footway"}, {"footway", "crossing"}, {"bicycle", "yes"}, {"surface", "paved"}}},
      {"AD", {{"highway", "footway"}, {"bicycle", "yes"}, {"surface", "paved"}}},
      {"DE", {{"highway", "footway"}, {"bicycle", "yes"}, {"surface", "paved"}}},
      {"EC", {{"highway", "footway"}, {"bicycle", "yes"}, {"surface", "paved"}}},
  };
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 100);
  auto map = gurka::buildtiles(layout, ways, {}, {}, "test/data/bicycle_sidewalk_cost");

  GraphReader reader(map.config.get_child("mjolnir"));
  const DirectedEdge* sidewalk = std::get<1>(gurka::findEdge(reader, map.nodes, "AB", "B"));
  ASSERT_NE(sidewalk, nullptr);
  EXPECT_EQ(sidewalk->use(), Use::kSidewalk);
  EXPECT_EQ(sidewalk->classification(), baldr::RoadClass::kServiceOther);
  const DirectedEdge* crossing = std::get<1>(gurka::findEdge(reader, map.nodes, "BC", "C"));
  ASSERT_NE(crossing, nullptr);
  EXPECT_EQ(crossing->use(), Use::kPedestrianCrossing);

  auto result = gurka::do_action(valhalla::Options::route, map, {"A", "C"}, "bicycle");
  gurka::assert::raw::expect_path(result, {"AB", "BC"});
}

// Same layout inside a Belarus admin boundary with no bicycle tagging anywhere: the country
// access defaults grant bicycle access on footways, so on stock tiles both routes are rideable
// and the sidewalk subtype must not cost more than the plain footways.
TEST(BicycleCost, CountryAccessSidewalkNotCostedAsServiceRoad) {
  const std::string ascii_map = R"(
      Q-------------------------------R
      |                               |
      |     A-------B-------C         |
      |     |               |         |
      |     D---------------E         |
      |                               |
      T-------------------------------S
  )";
  const gurka::ways ways = {
      {"QRSTQ", {}},
      {"AB", {{"highway", "footway"}, {"footway", "sidewalk"}, {"surface", "paved"}}},
      {"BC", {{"highway", "footway"}, {"footway", "sidewalk"}, {"surface", "paved"}}},
      {"AD", {{"highway", "footway"}, {"surface", "paved"}}},
      {"DE", {{"highway", "footway"}, {"surface", "paved"}}},
      {"EC", {{"highway", "footway"}, {"surface", "paved"}}},
  };
  const gurka::relations relations = {
      {{{gurka::way_member, "QRSTQ", "outer"}},
       {{"type", "boundary"},
        {"boundary", "administrative"},
        {"admin_level", "2"},
        {"name", "Belarus"}}},
  };

  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 100);
  const std::string workdir = "test/data/bicycle_country_sidewalk_cost";
  std::filesystem::create_directories(workdir);
  const std::string pbf_filename = workdir + "/map.pbf";
  const std::vector<std::string> input_files = {pbf_filename};
  gurka::detail::build_pbf(layout, ways, {}, relations, pbf_filename, false);

  auto config = test::make_config(workdir, {{"mjolnir.concurrency", "1"},
                                            {"mjolnir.tile_dir", workdir + "/tiles"},
                                            {"mjolnir.id_table_size", "1000"},
                                            {"mjolnir.admin", workdir + "/admin.sqlite"},
                                            {"mjolnir.timezone", workdir + "/not_needed.sqlite"}});
  gurka::map map{config, layout};
  ASSERT_TRUE(mjolnir::BuildAdminFromPBF(config.get_child("mjolnir"), input_files));
  mjolnir::build_tile_set(config, input_files, mjolnir::BuildStage::kInitialize,
                          mjolnir::BuildStage::kValidate);

  // the premise: country access defaults alone put bicycles on the sidewalk
  GraphReader reader(config.get_child("mjolnir"));
  const DirectedEdge* sidewalk = std::get<1>(gurka::findEdge(reader, map.nodes, "AB", "B"));
  ASSERT_NE(sidewalk, nullptr);
  EXPECT_EQ(sidewalk->use(), Use::kSidewalk);
  EXPECT_TRUE(sidewalk->forwardaccess() & kBicycleAccess);
  EXPECT_TRUE(sidewalk->reverseaccess() & kBicycleAccess);

  auto result = gurka::do_action(valhalla::Options::route, map, {"A", "C"}, "bicycle");
  gurka::assert::raw::expect_path(result, {"AB", "BC"});
}
