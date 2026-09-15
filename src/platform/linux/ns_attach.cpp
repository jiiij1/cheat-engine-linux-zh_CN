#include "core/ns_attach.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ce {

std::string resolveProcPath(pid_t pid, const std::string& rawPath) {
    if (rawPath.empty() || rawPath[0] != '/')
        return rawPath;                       // [heap], [stack], anon, "" — nothing to open
    // Same path, possibly a different file. A mount-namespace sandbox (Steam
    // Linux Runtime / pressure-vessel, Flatpak, Snap, a chroot) overlays standard
    // paths with its own tree, so /usr/lib/x86_64-linux-gnu/libc.so.6 exists on
    // both sides but is NOT the same file. Existence alone therefore cannot tell
    // which one the target is really running: reading our copy would hand back
    // symbol addresses that are wrong for the target (a wrong dlopen offset, a
    // call into the middle of an unrelated function, ...). Compare the file the
    // target sees (st_dev/st_ino) and prefer it whenever the two differ.
    std::string viaRoot = "/proc/" + std::to_string(pid) + "/root" + rawPath;
    struct stat here {}, there {};
    const bool hereExists = ::stat(rawPath.c_str(), &here) == 0;
    const bool thereExists = ::stat(viaRoot.c_str(), &there) == 0;
    if (thereExists && (!hereExists || here.st_dev != there.st_dev || here.st_ino != there.st_ino))
        return viaRoot;                       // same path here, different file there
    if (hereExists)
        return rawPath;                       // openable from the host already (common case)
    if (thereExists)
        return viaRoot;                       // exists only inside the target's namespace
    return rawPath;                           // neither exists; fail as before
}

// Parse the NSpid: line of /proc/<pid>/status into its whitespace-separated fields.
// Returns the fields in namespace order (outermost first, innermost last); empty on
// failure or when the field is absent (older kernels).
static std::vector<pid_t> nspidChain(pid_t pid) {
    std::vector<pid_t> out;
    std::ifstream st("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(st, line)) {
        if (line.rfind("NSpid:", 0) != 0) continue;
        std::istringstream iss(line.substr(6));
        pid_t v;
        while (iss >> v) out.push_back(v);
        break;
    }
    return out;
}

pid_t nsInnerPid(pid_t pid) {
    auto chain = nspidChain(pid);
    return chain.empty() ? pid : chain.back();
}

bool isPidNamespaced(pid_t pid) {
    return nspidChain(pid).size() > 1;
}

std::vector<pid_t> processDescendants(pid_t root) {
    // One pass over /proc to map every pid -> its parent's children list.
    std::map<pid_t, std::vector<pid_t>> children;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        const std::string name = entry.path().filename().string();
        char* end = nullptr;
        const long v = std::strtol(name.c_str(), &end, 10);
        if (*end != '\0' || v <= 0) continue;                 // not a pid dir
        std::ifstream st(entry.path() / "stat");
        std::string line;
        if (!std::getline(st, line)) continue;
        // Format: "pid (comm) state ppid ...". comm can contain spaces and ')', so
        // parse the fields after the LAST ')'.
        const auto rp = line.rfind(')');
        if (rp == std::string::npos) continue;
        std::istringstream after(line.substr(rp + 1));
        char state = 0; pid_t ppid = 0;
        if (!(after >> state >> ppid)) continue;
        children[ppid].push_back(static_cast<pid_t>(v));
    }

    std::vector<pid_t> out;
    std::set<pid_t> seen{root};
    std::queue<pid_t> q;
    q.push(root);
    while (!q.empty()) {
        const pid_t p = q.front();
        q.pop();
        auto it = children.find(p);
        if (it == children.end()) continue;
        for (pid_t c : it->second)
            if (seen.insert(c).second) { out.push_back(c); q.push(c); }
    }
    return out;
}

} // namespace ce
