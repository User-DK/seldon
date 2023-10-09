#pragma once
#include "model.hpp"
#include "network.hpp"
#include <vector>

namespace Seldon
{

struct ActivityAgentType
{
    double opinion  = 0; // x_i
    double activity = 0; // a_i
};

template<>
std::string Agent<ActivityAgentType>::to_string() const
{
    return fmt::format( "{}, {}", data.opinion, data.activity );
};

class ActivityAgentModel : public Model<Agent<ActivityAgentType>>
{

private:
    double max_opinion_diff = 0;
    Network & network;
    std::vector<AgentT> agents_current_copy;
    // Model-specific parameters 
    double dt = 0.1; // Timestep for the integration of the coupled ODEs
    // Various free parameters 
    int m = 10; // Number of agents contacted, when the agent is active
    double eps = 0.01; // Minimum activity epsilon; a_i belongs to [epsilon,1] 
    double gamma = 2.1; // Exponent of activity power law distribution of activities 
    double alpha; // Controversialness of the issue, must be greater than 0. 
    double homophily = 0; // aka beta. if zero, agents pick their interaction partners at random
    // Reciprocity aka r. probability that when agent i contacts j via weighted reservoir sampling
    // j also sends feedback to i. So every agent can have more than m incoming connections 
    double reciprocity = 0.5; 
    double K; // Social interaction strength; K>0   

public:
    double convergence_tol = 1e-12;

    ActivityAgentModel( int n_agents, Network & network );

    // void iteration() override;
    // bool finished() overteration() override;
    // bool finished() override;
};

} // namespace Seldon