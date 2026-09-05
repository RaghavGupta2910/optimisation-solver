#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace benchmark {

// ============================================================
// BenchmarkInstance
// ============================================================

struct BenchmarkInstance {
    std::string name;   // e.g. "instance_01.mps"
    std::string path;    // full path to the file
};

// ============================================================
// InstanceManager
//
// Discovers benchmark instance files from a directory. Does not
// run anything itself — just produces the list that
// runBenchmarkSuite() consumes.
// ============================================================

class InstanceManager {

public:

    // Scans `directoryPath` (non-recursive) for files ending in
    // `extension` (default ".mps") and returns them sorted by name.
    static std::vector<BenchmarkInstance> discoverInstances(
        const std::string& directoryPath,
        const std::string& extension = ".mps"
    ) {
        std::vector<BenchmarkInstance> instances;

        namespace fs = std::filesystem;

        if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
            return instances;
        }

        for (const auto& entry : fs::directory_iterator(directoryPath)) {

            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() != extension) {
                continue;
            }

            BenchmarkInstance instance;
            instance.name = entry.path().filename().string();
            instance.path = entry.path().string();

            instances.push_back(instance);
        }

        std::sort(
            instances.begin(),
            instances.end(),
            [](const BenchmarkInstance& a, const BenchmarkInstance& b) {
                return a.name < b.name;
            }
        );

        return instances;
    }
};

} // namespace benchmark