#include "models/ActivityDrivenModel.hpp"
#include "util/math.hpp"
#include <cstddef>
#include <random>
#include <stdexcept>

Seldon::ActivityAgentModel::ActivityAgentModel( int n_agents, Network & network, std::mt19937 & gen )
        : Model<Seldon::ActivityAgentModel::AgentT>( n_agents ),
          network( network ),
          agents_current_copy( std::vector<AgentT>( n_agents ) ),
          gen( gen )
{ }

void Seldon::ActivityAgentModel::get_agents_from_power_law()
{
    std::uniform_real_distribution<> dis_opinion( -1, 1 ); // Opinion initial values
    power_law_distribution<> dist_activity( eps, gamma );

    // Initial conditions for the opinions, initialize to [-1,1]
    // The activities should be drawn from a power law distribution
    for( size_t i = 0; i < agents.size(); i++ )
    {
        agents[i].data.opinion = dis_opinion( gen ); // Draw the opinion value
        // Draw from a power law distribution (1-gamma)/(1-eps^(1-gamma)) * a^(-gamma)
        agents[i].data.activity = dist_activity( gen );
    }
}

void Seldon::ActivityAgentModel::iteration()
{
    Model<AgentT>::iteration();

    std::uniform_real_distribution<> dis_activation( 0.0, 1.0 );    // Opinion initial values
    std::uniform_real_distribution<> dis_reciprocation( 0.0, 1.0 ); // Opinion initial values
    std::vector<size_t> contacted_agents{};

    reciprocal_edge_buffer.clear(); // Clear the reciprocal edge buffer

    for( size_t idx_agent = 0; idx_agent < network.n_agents(); idx_agent++ )
    {
        // Test if the agent is activated
        bool activated = dis_activation( gen ) < agents[idx_agent].data.activity;

        if( activated )
        {

            // Implement the weight for the probability of agent `idx_agent` contacting agent `j`
            // Not normalised since this is taken care of by the reservoir sampling
            auto weight_callback = [idx_agent, this]( size_t j )
            {
                if( idx_agent == j ) // The agent does not contact itself
                    return 0.0;
                return std::pow(
                    std::abs( this->agents[idx_agent].data.opinion - this->agents[j].data.opinion ), -this->homophily );
            };

            Seldon::reservoir_sampling_A_ExpJ( m, network.n_agents(), weight_callback, contacted_agents, gen );

            // Fill the outgoing edges into the reciprocal edge buffer
            for( const auto & idx_outgoing : contacted_agents )
            {
                reciprocal_edge_buffer.insert(
                    { idx_agent, idx_outgoing } ); // insert the edge idx_agent -> idx_outgoing
            }

            // Set the *outgoing* edges
            network.set_neighbours_and_weights( idx_agent, contacted_agents, 1.0 );
        }
        else
        {
            network.set_neighbours_and_weights( idx_agent, {}, {} );
        }
    }

    // Reciprocity check
    for( size_t idx_agent = 0; idx_agent < network.n_agents(); idx_agent++ )
    {
        // Get the outgoing edges
        network.get_neighbours( idx_agent, contacted_agents );
        // For each outgoing edge we check if the reverse edge already exists
        for( const auto & idx_outgoing : contacted_agents )
        {
            // If the edge is not reciprocated
            if( !reciprocal_edge_buffer.contains( { idx_outgoing, idx_agent } ) )
            {
                if( dis_reciprocation( gen ) < reciprocity )
                {
                    network.push_back_neighbour_and_weight( idx_outgoing, idx_agent, 1.0 );
                }
            }
        }
    }

    network.transpose(); // transpose the network, so that we have incoming edges

    // Integrate the ODE using 4th order Runge-Kutta
    auto k1_buffer = std::vector<double>();
    auto k2_buffer = std::vector<double>();
    auto k3_buffer = std::vector<double>();
    auto k4_buffer = std::vector<double>();

    // k_1 =   hf(x_n,y_n)
    get_euler_slopes( k1_buffer );

    // k_2  =   hf(x_n+1/2h,y_n+1/2k_1)
    get_euler_slopes( k1_buffer, 0.5, k2_buffer );

    // k_3  =   hf(x_n+1/2h,y_n+1/2k_2)
    get_euler_slopes( k2_buffer, 0.5, k3_buffer );

    // k_4  =   hf(x_n+h,y_n+k_3)
    get_euler_slopes( k3_buffer, 1.0, k4_buffer );

    // Update the agent opinions
    for( size_t idx_agent = 0; idx_agent < network.n_agents(); ++idx_agent )
    {
        // y_(n+1) =   y_n+1/6k_1+1/3k_2+1/3k_3+1/6k_4+O(h^5)
        agents[idx_agent].data.opinion
            += ( k1_buffer[idx_agent] + 2 * k2_buffer[idx_agent] + 2 * k3_buffer[idx_agent] + k4_buffer[idx_agent] )
               / 6.0;
    }
}

void Seldon::ActivityAgentModel::get_euler_slopes( std::vector<double> & k_buffer )
{
    // k_1   =   hf(x_n,y_n)
    // h is the timestep
    auto neighbour_buffer = std::vector<size_t>();
    size_t j_index        = 0;

    k_buffer.resize( network.n_agents() );

    for( size_t idx_agent = 0; idx_agent < network.n_agents(); ++idx_agent )
    {
        network.get_neighbours( idx_agent, neighbour_buffer ); // Get the incoming neighbours
        k_buffer[idx_agent] = -agents[idx_agent].data.opinion;
        // Loop through neighbouring agents
        for( size_t j = 0; j < neighbour_buffer.size(); j++ )
        {
            j_index = neighbour_buffer[j];
            k_buffer[idx_agent] += K * ( std::tanh( alpha * agents[j_index].data.opinion ) );
        }
        // Multiply by the timestep
        k_buffer[idx_agent] *= dt;
    }
}

void Seldon::ActivityAgentModel::get_euler_slopes(
    std::vector<double> & k_previous, double factor, std::vector<double> & k_buffer )
{
    // h is the timestep
    auto neighbour_buffer = std::vector<size_t>();
    size_t j_index        = 0;

    k_buffer.resize( network.n_agents() );

    for( size_t idx_agent = 0; idx_agent < network.n_agents(); ++idx_agent )
    {
        network.get_neighbours( idx_agent, neighbour_buffer ); // Get the incoming neighbours
        k_buffer[idx_agent] = -( agents[idx_agent].data.opinion + factor * k_previous[idx_agent] );
        // Loop through neighbouring agents
        for( size_t j = 0; j < neighbour_buffer.size(); j++ )
        {
            j_index = neighbour_buffer[j];
            k_buffer[idx_agent] += K * ( std::tanh( alpha * ( agents[j_index].data.opinion + factor * k_previous[j_index] ) ) );
        }
        // Multiply by the timestep
        k_buffer[idx_agent] *= dt;
    }
}