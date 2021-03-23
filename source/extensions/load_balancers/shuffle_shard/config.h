#pragma once

#include "envoy/common/pure.h"

#include "envoy/upstream/load_balancer.h"
#include "envoy/config/typed_config.h"

#include "envoy/registry/registry.h"
#include "envoy/config/cluster/v3/cluster.pb.h"



namespace Envoy {
namespace Extensions {
namespace LoadBalancer {
namespace ShuffleShard {

class LoadBalancerFactory : public Upstream::LoadBalancerFactory {

public:
  std::string name() const override;
  Upstream::LoadBalancerSharedPtr create();
  Upstream::LoadBalancerSharedPtr create(const Upstream::ClusterInfoConstSharedPtr& , Upstream::LoadBalancerFactoryContext&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(LoadBalancerFactory);

} // namespace ShuffleShard
} // namespace LoadBalancer
} // namespace Extensions
} // namespace Envoy
