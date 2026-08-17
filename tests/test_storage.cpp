#include "doctest.h"

#include "Core/PersistentStorage.h"
#include "Core/PlayerPrefs.h"
#include "Core/SaveSystem.h"
#include "SmokeTestSupport.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
struct StorageFixture {
    test_support::TempDirectory temp{"storage"};

    StorageFixture() {
        PersistentStorage::SetRootOverrideForTesting(temp.Path());
        REQUIRE(PersistentStorage::ConfigureRuntime("Studio", "Game"));
        PlayerPrefs::ResetCacheForTesting();
    }
    ~StorageFixture() {
        PlayerPrefs::ResetCacheForTesting();
        PersistentStorage::ClearRootOverrideForTesting();
    }
};

std::string ReadText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("PersistentStorage validates segments and isolates editor projects") {
    CHECK(PersistentStorage::IsSafePathSegment("Molga Studio"));
    CHECK_FALSE(PersistentStorage::IsSafePathSegment("../Studio"));
    CHECK_FALSE(PersistentStorage::IsSafePathSegment("Studio/Game"));
    CHECK_FALSE(PersistentStorage::IsSafePathSegment(std::string("Bad\nName")));

    test_support::TempDirectory project{"project-hash"};
    const auto hashA = PersistentStorage::StableProjectHash(project.Path());
    const auto hashB = PersistentStorage::StableProjectHash(project.Path() / ".");
    CHECK(hashA == hashB);
}

TEST_CASE("PersistentStorage atomic failure preserves the prior file") {
    StorageFixture fixture;
    const fs::path path = fixture.temp.Path() / "atomic.json";
    REQUIRE(PersistentStorage::AtomicWriteText(path, "old"));
    PersistentStorage::FailNextAtomicReplaceForTesting();
    CHECK_FALSE(PersistentStorage::AtomicWriteText(path, "new"));
    CHECK(ReadText(path) == "old");
}

TEST_CASE("PlayerPrefs preserves types, dirty state, and reloads from disk") {
    StorageFixture fixture;

    PlayerPrefs::Set("bool", true);
    PlayerPrefs::Set("int", 42);
    PlayerPrefs::Set("double", 3.25);
    PlayerPrefs::Set("string", "한글 설정");
    CHECK(PlayerPrefs::IsDirty());
    CHECK(PlayerPrefs::Get("bool", false));
    CHECK(PlayerPrefs::Get("int", 0) == 42);
    CHECK(PlayerPrefs::Get("double", 0.0) == doctest::Approx(3.25));
    CHECK(PlayerPrefs::Get("string", std::string{}) == "한글 설정");

    // Type mismatches do not silently coerce.
    CHECK(PlayerPrefs::Get("int", false) == false);
    CHECK(PlayerPrefs::Get("double", 7) == 7);
    REQUIRE(PlayerPrefs::Save());
    CHECK_FALSE(PlayerPrefs::IsDirty());

    PlayerPrefs::ResetCacheForTesting();
    CHECK(PlayerPrefs::GetInt("int") == 42);
    CHECK(PlayerPrefs::GetString("string") == "한글 설정");
    CHECK(PlayerPrefs::HasKey("bool"));
    PlayerPrefs::DeleteKey("bool");
    CHECK_FALSE(PlayerPrefs::HasKey("bool"));
    REQUIRE(PlayerPrefs::Shutdown());
}

TEST_CASE("PlayerPrefs does not automatically overwrite corrupt JSON") {
    StorageFixture fixture;
    test_support::WriteText(fixture.temp.Path() / "prefs.json", "{broken");
    PlayerPrefs::ResetCacheForTesting();

    CHECK(PlayerPrefs::GetInt("missing", 9) == 9);
    PlayerPrefs::SetInt("answer", 42);
    CHECK_FALSE(PlayerPrefs::Save());
    CHECK(ReadText(fixture.temp.Path() / "prefs.json") == "{broken");

    PlayerPrefs::DeleteAll();
    PlayerPrefs::SetInt("answer", 42);
    REQUIRE(PlayerPrefs::Save());
    CHECK(ReadText(fixture.temp.Path() / "prefs.json").find("answer") !=
          std::string::npos);
}

TEST_CASE("SaveSystem round trips JSON slots and rejects traversal") {
    StorageFixture fixture;
    nlohmann::json payload = {
        {"stage", 2},
        {"cleared", true},
        {"player", {{"x", 12.5}, {"name", "용사"}}}
    };

    REQUIRE(SaveSystem::SaveSlot("slot-01", payload));
    CHECK(SaveSystem::SlotExists("slot-01"));
    nlohmann::json restored;
    REQUIRE(SaveSystem::LoadSlot("slot-01", restored));
    CHECK(restored == payload);

    CHECK_FALSE(SaveSystem::SaveSlot("../escape", payload));
    CHECK_FALSE(SaveSystem::SaveSlot("bad/name", payload));
    CHECK_FALSE(SaveSystem::SaveSlot(std::string(65, 'a'), payload));
    CHECK(SaveSystem::DeleteSlot("slot-01"));
    CHECK_FALSE(SaveSystem::SlotExists("slot-01"));
}

TEST_CASE("SaveSystem rejects corrupt and unsupported slot envelopes") {
    StorageFixture fixture;
    const fs::path saves = fixture.temp.Path() / "Saves";
    test_support::WriteText(saves / "corrupt.json", "nope");
    test_support::WriteText(
        saves / "future.json",
        R"({"schemaVersion":2,"data":{"stage":99}})");

    nlohmann::json output = {{"unchanged", true}};
    CHECK_FALSE(SaveSystem::LoadSlot("corrupt", output));
    CHECK(output == nlohmann::json{{"unchanged", true}});
    CHECK_FALSE(SaveSystem::LoadSlot("future", output));
    CHECK(output == nlohmann::json{{"unchanged", true}});
}

TEST_CASE("serialization failures return false and preserve existing storage") {
    StorageFixture fixture;

    PlayerPrefs::SetString("name", "valid");
    REQUIRE(PlayerPrefs::Save());
    const fs::path prefsPath = fixture.temp.Path() / "prefs.json";
    const std::string originalPrefs = ReadText(prefsPath);

    PlayerPrefs::SetString("name", std::string("bad\xFF", 4));
    CHECK_FALSE(PlayerPrefs::Save());
    CHECK(PlayerPrefs::IsDirty());
    CHECK(ReadText(prefsPath) == originalPrefs);

    const nlohmann::json validSlot = {{"name", "valid"}};
    REQUIRE(SaveSystem::SaveSlot("safe", validSlot));
    const fs::path slotPath = fixture.temp.Path() / "Saves" / "safe.json";
    const std::string originalSlot = ReadText(slotPath);

    const nlohmann::json invalidSlot = {
        {"name", std::string("bad\xFF", 4)}
    };
    CHECK_FALSE(SaveSystem::SaveSlot("safe", invalidSlot));
    CHECK(ReadText(slotPath) == originalSlot);
}
