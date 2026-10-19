#include <catch2/catch_test_macros.hpp>
#include "../../titles/dash_1888/src/system_ui_request.h"
#include <array>

namespace {
std::vector<uint8_t> Bytes(std::initializer_list<uint32_t> words) {
  std::vector<uint8_t> out;
  for (auto w : words) for (unsigned i = 0; i < 4; ++i) out.push_back(uint8_t(w >> (24 - 8 * i)));
  return out;
}
}
TEST_CASE("Retail 1888 adapts Signin controller count and flags without shifting the user", "[dashboard1888-ui]") {
  using namespace dash1888;
  std::vector<uint8_t> out; uint32_t user = 9;
  for (const auto owner : {0u, 1u, 3u, 0xFFu}) {
    const auto legacy = Bytes({owner, 1, 0x400, 0});
    REQUIRE(AdaptRequest(0x21001, legacy, out, user)); CHECK(user == owner); CHECK(out == legacy);
    REQUIRE(AdaptRequest(0x21001, Bytes({0, owner, 1, 0x400, 0}), out, user));
    CHECK(user == owner); CHECK(out == legacy);
  }
  CHECK_FALSE(AdaptRequest(0x21001, Bytes({1, 0, 1, 0, 0}), out, user));
  CHECK_FALSE(AdaptRequest(0x21001, Bytes({4, 1, 0, 0}), out, user));
}
TEST_CASE("Retail 1888 keeps CreateProfile Guide and card original request shapes", "[dashboard1888-ui]") {
  using namespace dash1888;
  std::vector<uint8_t> out; uint32_t user = 9;
  REQUIRE(AdaptRequest(0x2100E, Bytes({2, 0, 0, 0}), out, user)); CHECK(user == 2); CHECK(out.size() == 16);
  CHECK_FALSE(AdaptRequest(0x2100E, std::vector<uint8_t>(136), out, user));
  REQUIRE(AdaptRequest(0x21015, Bytes({0, 3, 0}), out, user)); CHECK(user == 3); CHECK(out.empty());
  CHECK_FALSE(AdaptRequest(0x21015, Bytes({0}), out, user)); // Message box is not Guide.
  CHECK_FALSE(AdaptRequest(0x21015, Bytes({1, 0, 0}), out, user));
  CHECK_FALSE(AdaptRequest(0x21015, Bytes({0, 4, 0}), out, user));
  auto card = Bytes({1, 0, 0, 0, 0, 0}); card.resize(544);
  REQUIRE(AdaptRequest(0x21008, card, out, user)); CHECK(user == 1); CHECK(out.size() == 24);
  card[500] = 1; CHECK_FALSE(AdaptRequest(0x21008, card, out, user));
  CHECK_FALSE(AdaptRequest(0x21002, {}, out, user));
}
TEST_CASE("Retail 1888 rejects incomplete UI buffers before reading fields", "[dashboard1888-ui]") {
  using namespace dash1888;
  std::vector<uint8_t> out; uint32_t user = 9;
  for (const auto message : {0x21001u, 0x2100Eu, 0x21015u, 0x21008u}) {
    const auto* app = ApplicationFor(message); REQUIRE(app);
    for (uint32_t size = 0; size < app->size; ++size)
      CHECK_FALSE(AdaptRequest(message, std::vector<uint8_t>(size), out, user));
  }
}
