#include <iostream>
#include "DOTS.hpp"


struct Position{
        float x;
        float y;
        float z;
};

struct Velocity{
        float x;
        float y;
        float z;
};

struct Health{
        unsigned value;
};

DOTS::Entities<1000, 100, 1024*16, Position, Velocity, Health> e;
DOTS::JobSystem<64> j(false);

int main(){
        std::cout<<j.amountOfWorkers()<<std::endl;
        auto first = e.createEntity();
        auto second = e.createEntity();
        auto third = e.createEntity();
        e.addComponents(first, Position{1, 2, 3});
        e.addComponents(second, Position{10, 20, 30}, Velocity{10, 0, 1});
        e.addComponents(first, Velocity{1,1,1});
        e.addComponents(third, Position{1, 2, 3});
        e.delComponents<Velocity>(second);
        for(auto subview : e.select<Position, Velocity>()) j.schedule([subview]{
                const auto size = subview.size();
                auto positions = subview.write<Position>();
                auto velocities = subview.read<Velocity>();
                for(unsigned i = 0; i<size; ++i){
                       positions[i].x += velocities[i].x;
                       positions[i].y += velocities[i].y;
                       positions[i].z += velocities[i].z;
                }
        });
        //e.destroyEntity(third);
        //e.delComponents<Position>(third);
        j.scheduleNotConcurrent([]{
                for(auto subview : e.select<Position>()){
                        auto size = subview.size();
                        auto positions = subview.read<Position>();
                        auto ids = subview.readId();
                        for(unsigned i = 0; i<size; ++i){
                                std::cout<<"Entity "<<ids[i]<<" position: "<<positions[i].x<<" "<<positions[i].y<<" "<<positions[i].z<<std::endl;
                        }
                }
        });
        int i;
        while (true){
                std::cin >> i;
                if(i == -1) break;
        }
        return 0;
}