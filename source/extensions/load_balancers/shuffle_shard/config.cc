#include "extensions/load_balancers/shuffle_shard/config.h"
#include "envoy/upstream/load_balancer.h"

#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.h"
#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.validate.h"

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/config/metrics/v3/stats.pb.validate.h"

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancer {
namespace ShuffleShard {

std::string LoadBalancerFactory::name() const { return "envoy.load_balancers.shuffle_shard"; }


Upstream::LoadBalancerSharedPtr LoadBalancerFactory::create() {
  std::cout << "ShuffleShard::create::start" << std::endl;
  std::cout << "ShuffleShard::create::end" << std::endl;
  return nullptr;
};

ProtobufTypes::MessagePtr LoadBalancerFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::load_balancers::shuffle_shard::v3::ShuffleShardConfig>();
}

Upstream::LoadBalancerSharedPtr LoadBalancerFactory::create(const Upstream::ClusterInfoConstSharedPtr& , Upstream::LoadBalancerFactoryContext& ) {
      std::cout << "ShuffleShard::create::start-end" << std::endl;
      return nullptr;
}


// /**
//  * Static registration for the wasm factory. @see RegistryFactory.
//  */
REGISTER_FACTORY(LoadBalancerFactory, Upstream::LoadBalancerFactory);

} // namespace ShuffleShard
} // namespace LoadBalancer
} // namespace Extensions
} // namespace Envoy
