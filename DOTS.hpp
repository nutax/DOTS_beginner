#include <stdint.h>
#include <stdexcept>
#include <bitset>
#include <limits>
#include <cstring>
#include <type_traits>
#include <functional>
#include <vector>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <algorithm>

// By José Ignacio Huby Ochoa

namespace DOTS{
        namespace{
                constexpr bool isPrime(uint16_t n) noexcept {
                        if (n <= 1) return false;
                        for (uint16_t i = 2; i*i < n; i++) if (n % i == 0) return false;
                        return true;
                }
                constexpr uint16_t FirstGreaterPrime(uint16_t n) noexcept {
                        do{++n;} while(!isPrime(n));
                        return n;
                }

                template<typename T, typename... Ts>
                struct getTypeIndex;

                template<typename T, typename A, typename... Ts>
                struct getTypeIndex<T, A, Ts...>{
                static constexpr uint8_t value = 1 + getTypeIndex<T, Ts...>::value;
                };

                template<typename T, typename... Ts>
                struct getTypeIndex<T, T, Ts...>{
                static constexpr uint8_t value = 0;
                };

                template<typename... Ts>
                struct isTypePresent{
                        static constexpr bool value = false;
                };

                template<typename T, typename A, typename... Ts>
                struct isTypePresent<T, A, Ts...>{
                        static constexpr bool value = isTypePresent<T, Ts...>::value;
                };

                template<typename T, typename... Ts>
                struct isTypePresent<T, T, Ts...>{
                        static constexpr bool value = true;
                };
        };

using EntityID = uint32_t;
using Archetype = uint32_t;

template<uint16_t MAX_ENTITIES, uint16_t MAX_CHUNKS, uint16_t CHUNK_SIZE, typename... Components>
class Entities{
        
        static constexpr uint16_t MAP_CAPACITY_ENTITIES = FirstGreaterPrime(MAX_ENTITIES);
        static constexpr uint16_t MAP_CAPACITY_ARCHETYPES = FirstGreaterPrime(MAX_CHUNKS);
        static constexpr uint16_t CHUNK_BUFFER_SIZE = CHUNK_SIZE - 2 - 2 - 4 - 4 - 4*sizeof...(Components);
        static constexpr uint16_t COMPONENT_SIZE[] = {sizeof(Components)...,};

        #define MAP_UNUSED_SPACE 0
        #define MAP_DIRTY_SPACE std::numeric_limits<uint16_t>::max()

        struct EntityPosition{
                uint16_t archetype_index;
                uint16_t chunk_row;
        };
        struct Chunk{
                uint16_t size;
                uint16_t capacity;
                uint16_t* index;
                EntityID* id;
                uint8_t* component[sizeof...(Components)];
                uint8_t buffer[CHUNK_BUFFER_SIZE];
        };

        
        EntityID newEntityID = 1;
        EntityID* entities_ids;
        EntityPosition* entities_positions;

        uint16_t newChunkIndex = 0;
        std::bitset<MAP_CAPACITY_ARCHETYPES> full_chunks;
        std::bitset<MAP_CAPACITY_ARCHETYPES> not_empty_chunks;
        Archetype archetypes[MAP_CAPACITY_ARCHETYPES];
        uint16_t archetypes_chunks_indexes[MAP_CAPACITY_ARCHETYPES];

        Chunk* chunks;

        template<typename... Subset>
        struct getArchetype{
                static constexpr Archetype value = 0;
        };
        
        template<typename Component, typename... Subset>
        struct getArchetype<Component, Subset...>{
                static constexpr Archetype value = (1<<getTypeIndex<Component, Components...>::value) | getArchetype<Subset...>::value;
        };

        inline unsigned findEntityMapIndex(EntityID id){
                unsigned i = id%MAP_CAPACITY_ENTITIES;
                unsigned unvisiteds = MAP_CAPACITY_ENTITIES;
                while (0<unvisiteds){
                        if(entities_ids[i] == id)
                                return i;
                        if(entities_ids[i] == MAP_UNUSED_SPACE || entities_ids[i] == MAP_DIRTY_SPACE) 
                                break;
                        i = (i+1)%MAP_CAPACITY_ENTITIES;
                        --unvisiteds;
                }
                throw std::runtime_error("Entity not found!");
                return 0;
        }

        inline unsigned findAvailableEntityMapIndex(EntityID id){
                unsigned i = id%MAP_CAPACITY_ENTITIES;
                unsigned unvisiteds = MAP_CAPACITY_ENTITIES;
                while (0<unvisiteds){
                        if(entities_ids[i] == id || entities_ids[i] == MAP_UNUSED_SPACE)
                                return i;
                        if(entities_ids[i] == MAP_DIRTY_SPACE) 
                                break;
                        i = (i+1)%MAP_CAPACITY_ENTITIES;
                        --unvisiteds;
                }
                if(unvisiteds == 0) throw std::runtime_error("Out of space!");
                const unsigned first_dirty = i;
                i = (i+1)%MAP_CAPACITY_ENTITIES;
                --unvisiteds;
                while (0<unvisiteds){
                        if(entities_ids[i] == id)
                                return i;
                        if(entities_ids[i] == MAP_UNUSED_SPACE)
                                break;
                        i = (i+1)%MAP_CAPACITY_ENTITIES;
                        --unvisiteds;
                }
                return first_dirty;
        }

        inline void setupChunk(Archetype archetype, Chunk& chunk){
                chunk.size = 0;
                chunk.capacity = sizeof(uint16_t) + sizeof(EntityID);
                unsigned i;
                for(i = 0; i<sizeof...(Components); ++i){
                        if(archetype & (1<<i)){
                                chunk.capacity += COMPONENT_SIZE[i];
                        }
                }
                chunk.capacity = CHUNK_BUFFER_SIZE/chunk.capacity;
                chunk.index = (uint16_t*)chunk.buffer;
                chunk.id = (EntityID*)(chunk.buffer + chunk.capacity*sizeof(uint16_t));
                unsigned sum = sizeof(uint16_t) + sizeof(EntityID);
                for(i = 0; i<sizeof...(Components); ++i){
                        if(archetype & (1<<i)){
                                chunk.component[i] = chunk.buffer + chunk.capacity*sum;
                                sum += COMPONENT_SIZE[i];
                        }else{
                                chunk.component[i] = nullptr;
                        }
                }
        }

        inline unsigned findAvailableArchetypeMapIndex(Archetype archetype){
                unsigned i = archetype%MAP_CAPACITY_ARCHETYPES;
                while(
                        (archetypes[i] != archetype || full_chunks[i])
                        && archetypes[i] != MAP_UNUSED_SPACE
                ){i = (i+1)%MAP_CAPACITY_ARCHETYPES;}
                return i;
        }

        template<typename NewComponent, typename... NewComponents>
        inline void insertComponentsToChunk(Chunk& chunk, const NewComponent& component, const NewComponents&... components){
                std::memcpy(chunk.component[getTypeIndex<NewComponent, Components...>::value] + chunk.size*sizeof(NewComponent), &component, sizeof(NewComponent));
                insertComponentsToChunk(chunk, components...);
        }

        inline void insertComponentsToChunk(Chunk& chunk){

        }

        template<typename... NewComponents>
        inline void addEntityToArchetypeChunk(const EntityID id, const unsigned emi, const Archetype archetype, const NewComponents&... components){
                entities_ids[emi] = id;
                const unsigned ami = findAvailableArchetypeMapIndex(archetype);
                if(archetypes[ami] == MAP_UNUSED_SPACE){
                        archetypes[ami] = archetype;
                        archetypes_chunks_indexes[ami] = newChunkIndex++;
                        setupChunk(archetype, chunks[archetypes_chunks_indexes[ami]]);
                }
                Chunk& chunk = chunks[archetypes_chunks_indexes[ami]];
                chunk.index[chunk.size] = emi;
                chunk.id[chunk.size] = id;
                insertComponentsToChunk(chunk, components...);
                entities_positions[emi].archetype_index = ami;
                entities_positions[emi].chunk_row = chunk.size;
                chunk.size += 1;
                if(chunk.size == 1) not_empty_chunks.set(ami);
                else if(chunk.size == chunk.capacity) full_chunks.set(ami);
        }

        template<typename... NewComponents>
        inline void transferEntityToArchetypeChunk(const unsigned emi, const Archetype archetype, const NewComponents&... components){
                const unsigned ami = findAvailableArchetypeMapIndex(archetype);
                if(archetypes[ami] == MAP_UNUSED_SPACE){
                        archetypes[ami] = archetype;
                        archetypes_chunks_indexes[ami] = newChunkIndex++;
                        setupChunk(archetype, chunks[archetypes_chunks_indexes[ami]]);
                }
                Chunk& new_chunk = chunks[archetypes_chunks_indexes[ami]];
                Chunk& old_chunk = chunks[archetypes_chunks_indexes[entities_positions[emi].archetype_index]];
                const unsigned new_row = new_chunk.size;
                const unsigned old_row = entities_positions[emi].chunk_row;
                const unsigned old_last = old_chunk.size - 1;
                new_chunk.index[new_row] = old_chunk.index[old_row];
                old_chunk.index[old_row] = old_chunk.index[old_last];
                new_chunk.id[new_row] = old_chunk.id[old_row];
                old_chunk.id[old_row] = old_chunk.id[old_last];
                for(unsigned i = 0; i<sizeof...(Components); ++i){
                        if(old_chunk.component[i] != nullptr){
                                if(new_chunk.component[i] != nullptr) std::memcpy(new_chunk.component[i] + new_row*COMPONENT_SIZE[i], old_chunk.component[i] + old_row*COMPONENT_SIZE[i], COMPONENT_SIZE[i]);
                                std::memcpy(old_chunk.component[i] + old_row*COMPONENT_SIZE[i], old_chunk.component[i] + old_last*COMPONENT_SIZE[i], COMPONENT_SIZE[i]);
                        }
                }
                insertComponentsToChunk(new_chunk, components...);
                new_chunk.size += 1;
                old_chunk.size -= 1;
                if(new_chunk.size == new_chunk.capacity) full_chunks.set(ami);
                if(old_chunk.size == 0) not_empty_chunks.set(entities_positions[emi].archetype_index, false);
                entities_positions[old_chunk.index[old_row]].chunk_row = old_row;
                entities_positions[emi].chunk_row = new_row;
                entities_positions[emi].archetype_index = ami;
        }

        inline void removeEntityFromArchetypeChunk(const unsigned emi){
                entities_ids[emi] = MAP_DIRTY_SPACE;
                const unsigned ami = entities_positions[emi].archetype_index;
                Chunk& chunk = chunks[archetypes_chunks_indexes[ami]];
                const unsigned row = entities_positions[emi].chunk_row;
                const unsigned last_row = --chunk.size;
                if(row == last_row){
                        if(chunk.size == 0) not_empty_chunks.set(ami, false);
                        return;
                }
                entities_positions[chunk.index[last_row]].chunk_row = row;
                chunk.index[row] = chunk.index[last_row];
                chunk.id[row] = chunk.id[last_row];
                for(unsigned i = 0; i<sizeof...(Components); ++i){
                        if(chunk.component[i] != nullptr){
                                std::memcpy(chunk.component[i] + row*COMPONENT_SIZE[i], chunk.component[i] + last_row*COMPONENT_SIZE[i], COMPONENT_SIZE[i]);
                        }
                }
        }
        
        public:
        template<typename... Subset>
        class View{
                static constexpr Archetype archetype = getArchetype<std::decay_t<Subset>...>::value;
                Entities* entities;

                public:
                class SubView{
                        Chunk* chunk;

                        public:
                        SubView(Chunk* _chunk):chunk{_chunk}{}
                        ~SubView(){}
                        template<typename Component, std::enable_if_t<isTypePresent<Component, Subset...>::value, bool> = true>
                        inline Component* write() const {
                                return (Component*) chunk->component[getTypeIndex<Component, Components...>::value];
                        }
                        template<typename Component, std::enable_if_t<isTypePresent<Component, Subset...>::value, bool> = true>
                        inline const Component* read() const {
                                return (Component*) chunk->component[getTypeIndex<Component, Components...>::value];
                        }
                        inline const EntityID* readId() const {
                                return chunk->id;
                        }
                        inline unsigned size() const {
                                return chunk->size;
                        }

                };
                class iterator{
                        unsigned i;
                        Entities* entities;

                        public:
                        iterator(Entities* _entities): entities{_entities}{
                                for(i = 0; i<MAP_CAPACITY_ARCHETYPES; ++i)
                                        if(((archetype & entities->archetypes[i]) == archetype) && entities->not_empty_chunks[i])
                                                break;
                        }
                        iterator(): i{MAP_CAPACITY_ARCHETYPES}, entities{nullptr}{}
                        ~iterator(){}
                        bool operator!=(const iterator& other) const {return i != other.i;}
                        iterator operator++(){
                                for(++i; i<MAP_CAPACITY_ARCHETYPES; ++i)
                                        if(((archetype & entities->archetypes[i]) == archetype) && entities->not_empty_chunks[i])
                                                break;
                                return *this;
                        }
                        SubView operator*() const {
                                return SubView(entities->chunks + entities->archetypes_chunks_indexes[i]);
                        }
                };
                View(Entities* _entities):entities{_entities}{}
                ~View(){}
                iterator begin() const {return iterator(entities);}
                iterator end() const {return iterator();}
        };

        Entities(){
                entities_ids = new EntityID[MAP_CAPACITY_ENTITIES];
                entities_positions = new EntityPosition[MAP_CAPACITY_ENTITIES];
                chunks = new Chunk[MAX_CHUNKS];

                uint32_t i;
                for(i = 0; i<MAP_CAPACITY_ENTITIES; ++i) entities_ids[i] = MAP_UNUSED_SPACE;
                for(i = 0; i<MAP_CAPACITY_ARCHETYPES; ++i) archetypes[i] = MAP_UNUSED_SPACE;
        }
        ~Entities(){
                delete[] entities_ids;
                delete[] entities_positions;
                delete[] chunks;
        }
        inline EntityID createEntity(){
                return newEntityID++;
        }
        void destroyEntity(const EntityID id){
                removeEntityFromArchetypeChunk(findEntityMapIndex(id));
        }
        template<typename... NewComponents>
        void addComponents(const EntityID id, const NewComponents&... components){
                constexpr Archetype addition_archetype = getArchetype<NewComponents...>::value;
                const unsigned emi = findAvailableEntityMapIndex(id);
                if(entities_ids[emi] == MAP_UNUSED_SPACE || entities_ids[emi] == MAP_DIRTY_SPACE){
                        addEntityToArchetypeChunk(id, emi, addition_archetype, components...);
                }else{
                        const Archetype current_archetype = archetypes[entities_positions[emi].archetype_index];
                        transferEntityToArchetypeChunk(emi, addition_archetype | current_archetype, components...);
                }
        }
         template<typename... OldComponents>
        void delComponents(const EntityID id){
                constexpr Archetype subtraction_archetype = getArchetype<OldComponents...>::value;
                const unsigned emi = findEntityMapIndex(id);
                const Archetype current_archetype = archetypes[entities_positions[emi].archetype_index];
                const Archetype new_archetype = (subtraction_archetype ^ current_archetype) & current_archetype;
                if(new_archetype != 0) transferEntityToArchetypeChunk(emi, new_archetype);
                else removeEntityFromArchetypeChunk(emi);
        }
        template<typename... Subset>
        inline View<Subset...> select() {
                return View<Subset...>(this);
        }
};

template<unsigned JOBS_QUEUE_CAPACITY>
class JobSystem{
        std::function<void(void)> queue_jobs[JOBS_QUEUE_CAPACITY];
        unsigned queue_front = 0;
        unsigned queue_back = 0;
        unsigned queue_size = 0;
        std::mutex queue_lock;
        std::condition_variable queue_not_full;
        std::condition_variable queue_not_empty;

        unsigned nworkers;
        unsigned syncpoint_counter;
        std::mutex syncpoint_lock;
        std::condition_variable syncpoint_cv_1;
        std::condition_variable syncpoint_cv_2;

        const std::function<void(void)> SyncPointWait = [this]{
                std::unique_lock<std::mutex> ul(syncpoint_lock);
                syncpoint_cv_1.wait(ul, [this]{return syncpoint_counter < nworkers;});
                syncpoint_counter -= 1;
                ul.unlock();
                syncpoint_cv_2.notify_one();
        };
        const std::function<void(void)> SyncPointWake = [this]{
                {
                        std::lock_guard<std::mutex> lg(syncpoint_lock);
                        syncpoint_counter -= 1;
                }
                syncpoint_cv_1.notify_all();

                std::unique_lock<std::mutex> ul(syncpoint_lock);
                syncpoint_cv_2.wait(ul, [this]{return syncpoint_counter == 1;});
                syncpoint_counter = nworkers;
        };
        public:
        inline void schedule(const std::function<void(void)>& job){
                std::unique_lock<std::mutex> ul(queue_lock);
                queue_not_full.wait(ul, [this](){return queue_size != JOBS_QUEUE_CAPACITY;});
                queue_jobs[queue_back] = job;
                queue_back = (queue_back + 1) % JOBS_QUEUE_CAPACITY;
                ++queue_size;
                ul.unlock();
                queue_not_empty.notify_one();
        }
        inline void scheduleSyncPoint(){
                for(unsigned i = 1; i<nworkers; ++i)
                        schedule(SyncPointWait);
                schedule(SyncPointWake);
        }
        inline void scheduleNotConcurrent(const std::function<void(void)>& job){
                for(unsigned i = 1; i<nworkers; ++i)
                        schedule(SyncPointWait);
                schedule(job);
                schedule(SyncPointWake);
        }
        inline void work(){
                std::unique_lock<std::mutex> ul(queue_lock);
                queue_not_empty.wait(ul, [this](){return queue_size != 0;});
                auto job = queue_jobs[queue_front];
                queue_front = (queue_front + 1) % JOBS_QUEUE_CAPACITY;
                --queue_size;
                ul.unlock();
                queue_not_full.notify_one();
                job();
        }
        JobSystem(bool mainThreadWillWork){
                const auto ncores = std::thread::hardware_concurrency();
                nworkers = (1u < ncores) ? ncores : 1u;
                for (unsigned i = 0; i < nworkers; ++i){
                        std::thread worker([this] () {
                                while (true) this->work();
                        });
                        worker.detach();
                }
                if(mainThreadWillWork) nworkers += 1;
                syncpoint_counter = nworkers;
        }
        ~JobSystem(){}
        unsigned amountOfWorkers() const {return nworkers;}
        
};

};