#include "common/upstream/maglev_lb.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include <random>
#include <algorithm>

namespace Envoy {
namespace Upstream {

MaglevTable::MaglevTable(const NormalizedHostWeightVector& normalized_host_weights,
                         double max_normalized_weight, uint64_t table_size,
                         bool use_hostname_for_hashing, uint32_t shard_size,
                         LoadBalancerType lb_type, MaglevLoadBalancerStats& stats)
    : table_size_(table_size), shard_size_(shard_size), lb_type_(lb_type), stats_(stats) {
  // We can't do anything sensible with no hosts.
  ENVOY_LOG(info, "maglev: init table");
  if (normalized_host_weights.empty()) {
    return;
  }

  // Implementation of pseudocode listing 1 in the paper (see header file for more info).
  std::vector<TableBuildEntry> table_build_entries;
  table_build_entries.reserve(normalized_host_weights.size());
  for (const auto& host_weight : normalized_host_weights) {
    const auto& host = host_weight.first;
    const std::string& address =
        use_hostname_for_hashing ? host->hostname() : host->address()->asString();
    ASSERT(!address.empty());
    table_build_entries.emplace_back(host, HashUtil::xxHash64(address) % table_size_,
                                     (HashUtil::xxHash64(address, 1) % (table_size_ - 1)) + 1,
                                     host_weight.second);
  }

  table_.resize(table_size_);

  // Iterate through the table build entries as many times as it takes to fill up the table.
  uint64_t table_index = 0;
  for (uint32_t iteration = 1; table_index < table_size_; ++iteration) {
    for (uint64_t i = 0; i < table_build_entries.size() && table_index < table_size; i++) {
      TableBuildEntry& entry = table_build_entries[i];
      // To understand how target_weight_ and weight_ are used below, consider a host with weight
      // equal to max_normalized_weight. This would be picked on every single iteration. If it had
      // weight equal to max_normalized_weight / 3, then it would only be picked every 3 iterations,
      // etc.
      if (iteration * entry.weight_ < entry.target_weight_) {
        continue;
      }
      entry.target_weight_ += max_normalized_weight;
      uint64_t c = permutation(entry);
      while (table_[c] != nullptr) {
        entry.next_++;
        c = permutation(entry);
      }

      table_[c] = entry.host_;
      entry.next_++;
      entry.count_++;
      table_index++;
    }
  }

  uint64_t min_entries_per_host = table_size_;
  uint64_t max_entries_per_host = 0;
  for (const auto& entry : table_build_entries) {
    min_entries_per_host = std::min(entry.count_, min_entries_per_host);
    max_entries_per_host = std::max(entry.count_, max_entries_per_host);
  }
  stats_.min_entries_per_host_.set(min_entries_per_host);
  stats_.max_entries_per_host_.set(max_entries_per_host);

  if (ENVOY_LOG_CHECK_LEVEL(trace)) {
    for (uint64_t i = 0; i < table_.size(); i++) {
      ENVOY_LOG(trace, "maglev: i={} host={}", i,
                use_hostname_for_hashing ? table_[i]->hostname()
                                         : table_[i]->address()->asString());
    }
  }
}

void MaglevTable::chooseHosts(uint64_t hash, HostConstSharedPtr * hosts, uint8_t * max_hosts) const {
  if (table_.empty()) {
    return;
  }

  const uint64_t seed = hash;
  ENVOY_LOG(info, "maglev: shard_index=none hash={}", hash);
  std::mt19937 random(seed);
  bool unique;

  for (uint8_t i = 0; i < shard_size_; i++) {
    unique = false;
    for(uint64_t c = 0; !unique && c < shard_size_; c++) {
      unique = true;
      for (uint64_t j = 0; j < i; j++) {
        if (hosts[j] == table_[hash % table_size_]) {
          unique = false;
          hash++;
          break;
        }
      }
    }
    hosts[i] = table_[hash % table_size_];
    hash = random();
  }

  Envoy::Upstream::Host::Health max_health = Envoy::Upstream::Host::Health::Unhealthy;
  for (uint64_t i = 0; i < shard_size_; i++) {
    switch (hosts[i]->health()) {
    case Envoy::Upstream::Host::Health::Degraded:
      max_health = std::max(max_health, hosts[i]->health());
      break;
    case Envoy::Upstream::Host::Health::Healthy:
      max_health = std::max(max_health, hosts[i]->health());
      break;
    default:
      break;
    }
  }
  ENVOY_LOG(info, "maglev: health={}", max_health);

  uint8_t c = 0;
  for (uint64_t i = 0; i < shard_size_; i++) {
    if (hosts[i]->health() == max_health) {
      if (c != i)
        hosts[c] = hosts[i];
      c++;
    }
  }
  ENVOY_LOG(info, "maglev: hosts={}", c);
  *max_hosts = c;
}

HostConstSharedPtr MaglevTable::chooseHost(uint64_t hash, uint32_t attempt) const {
  if (table_.empty()) {
    return nullptr;
  }

  uint64_t shard_index;
  if (shard_size_ > 1) {
    switch(lb_type_) {
    case LoadBalancerType::Random:
      shard_index = random() % shard_size_;
      break;
    case LoadBalancerType::LeastRequest:
    case LoadBalancerType::RoundRobin:
      shard_index = shard_size_-1;
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }

    const uint64_t seed = hash;
    ENVOY_LOG(info, "maglev: shard_index={} hash={}", shard_index, hash);
    HostConstSharedPtr hosts [5] = { };
    uint64_t hashes [5] = { };
    std::mt19937 random(seed);
    bool unique;

    for (uint64_t i = 0; i <= shard_index; i++) {
      unique = false;
      // while (!unique) {
      for(uint64_t c = 0; !unique && c < shard_size_; c++) {
        unique = true;
        for (uint64_t j = 0; j < i; j++) {
          if (hosts[j] == table_[hash % table_size_]) {
            unique = false;
            hash++;
            break;
          }
        }
      }
      ENVOY_LOG(info, "maglev: index={} hash={}", i, hash);
      hashes[i] = hash % table_size_;
      hosts[i] = table_[hash % table_size_];
      hash = random();
    }

    // for (uint64_t i = 0; i <= shard_index; i++) {
    //
    // }
    uint64_t ret = 0;
    switch(lb_type_) {
    case LoadBalancerType::Random:
      return hosts[shard_index];
    case LoadBalancerType::LeastRequest:
      // bool least(HostConstSharedPtr i, HostConstSharedPtr j) { return i->stats().rq_active_ < j->stats().rq_active_; }
      for (uint64_t i = 1; i < shard_index; i++) {
        ENVOY_LOG(info, "maglev: comp()");
        ENVOY_LOG(info, "maglev: comp() hashes={}", *hashes);
        ENVOY_LOG(info, "maglev: comp() hash1={}", *(hashes+ret));
        ENVOY_LOG(info, "maglev: comp() host1={}", table_[*(hashes+ret)]);
        ENVOY_LOG(info, "maglev: comp() hash2={}", *(hashes+i));
        ENVOY_LOG(info, "maglev: comp() host2={}", table_[*(hashes+i)]);
        ENVOY_LOG(info, "maglev: comp({}, {})", table_[*(hashes+ret)]->stats().rq_active_.value(), table_[*(hashes+i)]->stats().rq_active_.value());
        if (table_[*(hashes+ret)]->stats().rq_active_.value() > table_[*(hashes+i)]->stats().rq_active_.value()) {
          ret = i;
        }
      }
      return table_[*(hashes+ret)];
    case LoadBalancerType::RoundRobin:

      for (uint64_t i = 1; i < shard_index; i++) {
        if (table_[*(hashes+ret)]->stats().rq_total_.value() > table_[*(hashes+i)]->stats().rq_total_.value()) {
          ret = i;
        }
      }
      return table_[*(hashes+ret)];
    //   bool round(HostConstSharedPtr i, HostConstSharedPtr j) { return i->stats().rq_total_ < j->stats().rq_total_; }
    //   return std::min_element(hosts, hosts+shard_index, round);
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    ENVOY_LOG(info, "maglev: not good");
    // return hosts[shard_index];
  }
  if (attempt > 0) {
    // If a retry host predicate is being applied, mutate the hash to choose an alternate host.
    // By using value with most bits set for the retry attempts, we achieve a larger change in
    // the hash, thereby reducing the likelihood that all retries are directed to a single host.
    hash ^= ~0ULL - attempt + 1;
  }

  return table_[hash % table_size_];
}

uint64_t MaglevTable::permutation(const TableBuildEntry& entry) {
  return (entry.offset_ + (entry.skip_ * entry.next_)) % table_size_;
}

MaglevLoadBalancer::MaglevLoadBalancer(
    const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>& config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : ThreadAwareLoadBalancerBase(priority_set, stats, runtime, random, common_config),
      scope_(scope.createScope("maglev_lb.")), stats_(generateStats(*scope_)),
      table_size_(config ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.value(), table_size,
                                                           MaglevTable::DefaultTableSize)
                         : MaglevTable::DefaultTableSize),
      use_hostname_for_hashing_(
          common_config.has_consistent_hashing_lb_config()
              ? common_config.consistent_hashing_lb_config().use_hostname_for_hashing()
              : false),
      hash_balance_factor_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          common_config.consistent_hashing_lb_config(), hash_balance_factor, 0)),
      shard_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          common_config.consistent_hashing_lb_config(), shard_size, 1)) {
  ENVOY_LOG(debug, "maglev table size: {}", table_size_);
  // The table size must be prime number.
  switch (common_config.consistent_hashing_lb_config().lb_policy()) {
  case envoy::config::cluster::v3::Cluster::CommonLbConfig::ConsistentHashingLbConfig::ROUND_ROBIN:
  // case envoy::config::cluster::v3::Cluster::ROUND_ROBIN:
  // // // case envoy::config::cluster::v3::Cluster::ROUND_ROBIN:
    lb_type_ = LoadBalancerType::RoundRobin;
    break;
  case envoy::config::cluster::v3::Cluster::CommonLbConfig::ConsistentHashingLbConfig::LEAST_REQUEST:
  // // case envoy::config::cluster::v3::Cluster::LEAST_REQUEST:
    lb_type_ = LoadBalancerType::LeastRequest;
    break;
  case envoy::config::cluster::v3::Cluster::CommonLbConfig::ConsistentHashingLbConfig::RANDOM:
  // case envoy::config::cluster::v3::Cluster::CommonLbConfig::ConsistentHashingLbConfig::LbPolicy::RANDOM:
    lb_type_ = LoadBalancerType::Random;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  // lb_type_ = common_config.consistent_hashing_lb_config().lb_policy();
  ENVOY_LOG(info, "maglev table size: {}", lb_type_);

  if (!Primes::isPrime(table_size_)) {
    throw EnvoyException("The table size of maglev must be prime number");
  }
}

MaglevLoadBalancerStats MaglevLoadBalancer::generateStats(Stats::Scope& scope) {
  return {ALL_MAGLEV_LOAD_BALANCER_STATS(POOL_GAUGE(scope))};
}

} // namespace Upstream
} // namespace Envoy
