#pragma once
// What a capture from the original remote contains, beyond the state decode_frames picks.
// Used to tell a button press from the remote's own unprompted resend: a resend is one
// burst of a single state, the same state the remote sent last.
#include <map>
#include <string>
#include <vector>

namespace fireplace {

// Each distinct state field from `code` in the capture, with how many frames carried it.
inline std::map<std::string, int> capture_states(const std::vector<int32_t> &raw, uint64_t code) {
  std::string hb = to_halfbits(raw), want = sync_id(code), bits;
  std::map<std::string, int> seen;
  for (size_t k = hb.find(want); k != std::string::npos; k = hb.find(want, k + 1))
    if (unmanchester(hb, k + want.size(), STATE_BITS, bits))
      seen[bits]++;
  return seen;
}

// One number per state, for comparing what the remote sent with what it sent last.
inline int state_key(bool off, int flame, int fan) { return off ? -1 : flame * 10 + fan; }

// "<state bits> x<count> (<state>)" for each state in the capture, for the log.
inline std::string frame_summary(const std::map<std::string, int> &seen) {
  std::string out;
  for (const auto &kv : seen) {
    bool off;
    int flame, fan;
    char buf[48];
    if (!lookup_state(kv.first, off, flame, fan))
      snprintf(buf, sizeof(buf), " x%d (unknown)", kv.second);
    else if (off)
      snprintf(buf, sizeof(buf), " x%d (off)", kv.second);
    else
      snprintf(buf, sizeof(buf), " x%d (flame %d fan %d)", kv.second, flame, fan);
    out += (out.empty() ? "" : ", ") + kv.first + buf;
  }
  return out;
}

}  // namespace fireplace
