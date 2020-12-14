#include "RuntimeCore/RuntimeCore.h"
#include "spdlog/spdlog.h"
#include "marl/finally.h"
#include "SakuraSTL.hpp"

#include "System/VirtualFileSystem.h"
#include "System/Log.h"

#include "Base/GUID.h"

#include "Codebase/Codebase.h"

#include "ECS/ECS.h"

#include "TransformComponents.h"
#include "Boids.h"
#include "TaskSystem/TaskSystem.h"
#include "RuntimeCore/RuntimeCore.h"
#include "kdtree.h"
#include <iostream>

#define forloop(i, z, n) for(auto i = std::decay_t<decltype(n)>(z); i<(n); ++i)
#define def static constexpr auto

namespace task_system = sakura::task_system;
namespace math = sakura::math;
using Vector3f = sakura::Vector3f;
using Quaternion = sakura::Quaternion;
using Rotator = sakura::Rotator;
using float4x4 = sakura::float4x4;
using IModule = sakura::IModule;

sakura::ecs::world ctx;

std::size_t calc_align(std::size_t n, std::size_t align)
{
	return ((n + align - 1) / align)* align;
}

template<class T>
task_system::Event Local2XSystem(task_system::ecs::pipeline& ppl, ecs::filters& filter)
{
	using namespace ecs;
	def paramList = boost::hana::make_tuple(
		// write
		param<T>,
		// read.
		param<const Translation>, param<const Rotation>, param<const Scale>
	);
	return task_system::ecs::schedule(ppl,
		*ppl.create_pass(filter, paramList),
		[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			auto o = operation{ paramList, pass, tk };
			const sakura::Vector3f* scales = o.get_parameter<const Scale>();
			const sakura::Vector3f* translations = o.get_parameter<const Translation>();
			const sakura::Quaternion* quaternions = o.get_parameter<const Rotation>();
			float4x4* l2ws = o.get_parameter<T>();

			forloop(i, 0, o.get_count())
			{
				const Vector3f scale = scales ? scales[i] : Vector3f::vector_one();
				const Vector3f translation = translations ? translations[i] : Vector3f::vector_zero();
				const Quaternion quaternion = quaternions ? quaternions[i] : Quaternion::identity();

				l2ws[i] = math::make_transform(translation, scale, quaternion);
			}
		});
}

task_system::Event RotationEulerSystem(task_system::ecs::pipeline& ppl)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter = {
		{complist<RotationEuler, Rotation>}
	};
	def paramList =
		boost::hana::make_tuple(param<const RotationEuler>, param<Rotation>);
	return task_system::ecs::schedule(
		ppl, *ppl.create_pass(filter, paramList),
		[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
            using namespace sakura::math;
            
			auto o = operation{ paramList, pass, tk };
			const sakura::Rotator* rotators = o.get_parameter<const RotationEuler>();

			auto rid = cid<Rotation>;
			Quaternion* quaternions = o.get_parameter<Rotation>();

			forloop(i, 0, o.get_count())
				quaternions[i] = math::quaternion_from_rotator(rotators[i]);
		});
}

task_system::Event Child2WorldSystem(task_system::ecs::pipeline& ppl)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter = {
		{complist<Child, LocalToWorld>},
		{},
		{complist<Parent, LocalToParent>} // from root
	};
	def paramList = boost::hana::make_tuple(
		// write
		param<LocalToWorld>,
		// read.
		param<const LocalToParent>, param<const Child>
	);
	struct children2World
	{
		static void solve(const float4x4& parent_l2w, const entity e)
		{
			float4x4 l2w = float4x4();
			auto child_l2w = static_cast<float4x4*>(ctx.get_owned_rw(e, cid<LocalToWorld>));
			const auto child_l2p = static_cast<const float4x4*>(ctx.get_owned_ro(e, cid<LocalToParent>));
			if (child_l2w && child_l2p)
			{
				l2w = sakura::math::multiply(parent_l2w, *child_l2p);
				*child_l2w = l2w;
			}

			index_t childSet[] = { cid<Child> };
			if (ctx.has_component(e, childSet))
			{
				ecs::value_type_t<Child> child_children = ctx.get_owned_ro(e, cid<Child>);
				for (auto& child : child_children)
				{
					children2World::solve(l2w, child);
				}
			}
		}
	};
	return task_system::ecs::schedule(ppl,
		*ppl.create_pass(filter, paramList),
		[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			auto o = operation{ paramList, pass, tk };
			const auto childrens = o.get_parameter<const Child>();
			float4x4* l2ws = o.get_parameter<LocalToWorld>();

			forloop(i, 0, o.get_count())
			{
				auto& children = childrens[i];
				for (const auto& child : children)
				{
					children2World::solve(l2ws[i], child);
				}
			}
		});
}

task_system::Event World2LocalSystem(task_system::ecs::pipeline& ppl)
{
	using namespace ecs;
	filters filter;
	filter.archetypeFilter = {
		{complist<LocalToWorld, WorldToLocal>}, //all
		{}, //any
		{} //none
	};
	def paramList = boost::hana::make_tuple(
		// write
		param<WorldToLocal>,
		// read.
		param<const LocalToWorld>
	);
	return task_system::ecs::schedule(ppl,
		*ppl.create_pass(filter, paramList),
		[](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			auto o = operation{ paramList, pass, tk };
			const float4x4* l2ws = o.get_parameter<const LocalToWorld>();
			float4x4* w2ls = o.get_parameter<WorldToLocal>();

			forloop(i, 0, o.get_count())
			{
				w2ls[i] = sakura::math::inverse(l2ws[i]);
			}
		});
}

template<class C, class T>
task_system::Event CopyComponent(task_system::ecs::pipeline& ppl, const ecs::filters& filter, ecs::shared_resource<T>& vector)
{
	using namespace ecs;
	def paramList = hana::tuple{ param<const T> };
	shared_entry shareList[] = { write(vector) };
	auto pass = ppl.create_pass(filter, paramList, shareList);
	vector->resize(pass->entityCount);
	return task_system::ecs::schedule(ppl, *pass,
		[vector](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
		{
			auto o = operation{ paramList, pass, tk };
			auto index = o.get_index();
			auto comps = o.get_parameter<const T>();
			forloop(i, 0, o.get_count())
				(*vector)[index + i] = comps[i];
		});
}

struct BoidPosition
{
	sakura::Vector3f value;
	BoidPosition(sakura::Vector3f value)
		:value(value) {}
	def dim = 3;
	using value_type = float;
	float operator[](size_t i) { return value.data_view()[i]; }
};

sakura::Vector3f nearest_position(const sakura::Vector3f& query, const std::vector<sakura::Vector3f>& searchTargets)
{
	float minDistance = std::numeric_limits<float>::max();
	sakura::Vector3f result;
	for (const auto& target : searchTargets)
	{
		float d = distance(query, target);
		if (d < minDistance)
		{
			minDistance = d;
			result = target;
		}
	}
	return result;
}

task_system::Event BoidsSystem(task_system::ecs::pipeline& ppl, float deltaTime)
{
	using namespace ecs;
	filters boidFilter;
	boidFilter.archetypeFilter =
	{
		{complist<Boid, Translation, Heading>}, //all
		{}, //any
		{}, //none
		{complist<Boid>} //shared
	};

	//build kdtree, exact headings
	auto positions = make_resource<std::vector<BoidPosition>>();
	auto headings = make_resource<std::vector<Heading>>();
	auto kdtree = make_resource<core::algo::kdtree<BoidPosition>>();
	{
		auto copyPositionJob = CopyComponent<Translation>(ppl, boidFilter, positions);
		CopyComponent<Heading>(ppl, boidFilter, headings);
		shared_entry shareList[] = { read(positions), write(kdtree) };
		task_system::ecs::schedule_custom(ppl, *ppl.create_custom_pass(shareList), [positions, kdtree]() mutable
			{
				kdtree->initialize(std::move(*positions));
			});
	}

	//collect targets and obstacles
	auto targets = make_resource<std::vector<sakura::Vector3f>>();
	{
		filters targetFilter;
		targetFilter.archetypeFilter =
		{
			{complist<BoidTarget, Translation>}
		};
		CopyComponent<Translation>(ppl, targetFilter, targets);
	}

	//build new headings
	auto newHeadings = make_resource<chunk_vector<sakura::Vector3f>>();
	{
		shared_entry shareList[] = { read(kdtree), read(headings), read(targets), write(newHeadings) };
		def paramList = hana::tuple{ param<const Heading>, param<const Translation>, param<const Boid> };
		auto pass = ppl.create_pass(boidFilter, paramList, shareList);
		newHeadings->resize(pass->entityCount);
		task_system::ecs::schedule(ppl, *pass,
			[headings, kdtree, targets, newHeadings](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
			{
				auto o = operation{ paramList, pass, tk };
				auto index = o.get_index();
				auto hds = o.get_parameter_owned<const Heading>();
				auto trs = o.get_parameter_owned<const Translation>();
				auto boid = o.get_parameter<const Boid>(); //这玩意是 shared
				std::vector<int> neighbers;
				chunk_vector<sakura::Vector3f> alignments;
				chunk_vector<sakura::Vector3f> separations;
				chunk_vector<sakura::Vector3f> targetings;
				alignments.resize(o.get_count());
				separations.resize(o.get_count());
				targetings.resize(o.get_count());
				forloop(i, 0, o.get_count())
				{
					neighbers.clear();
					kdtree->search_radius(trs[i], boid->SightRadius, neighbers);
					alignments[i] = sakura::Vector3f::vector_zero();
					separations[i] = sakura::Vector3f::vector_zero();
					for (int ng : neighbers)
					{
						alignments[i] += (*headings)[ng].value;
						separations[i] += (*kdtree)[ng].value;
					}
				}
				forloop(i, 0, o.get_count())
				{
					targetings[i] = nearest_position(trs[i], *targets);
				}
				forloop(i, 0, o.get_count())
				{
					sakura::Vector3f alignment = math::normalize(alignments[i] / neighbers.size() - hds[i]);
					sakura::Vector3f separation = math::normalize(neighbers.size() * trs[i] - separations[i]);
					sakura::Vector3f targeting = math::normalize(targetings[i] - trs[i]);
					(*newHeadings)[index + i] = math::normalize(alignment * boid->AlignmentWeight + separation * boid->SeparationWeight + targeting * boid->TargetWeight);
				}
			}, -1);
	}
	//
	{
		shared_entry shareList[] = { read(newHeadings) };
		def paramList = hana::tuple{ param<Heading>, param<Translation>, param<const Boid> };
		return task_system::ecs::schedule(ppl, *ppl.create_pass(boidFilter, paramList, shareList),
			[newHeadings, deltaTime](const task_system::ecs::pipeline& pipeline, const ecs::pass& pass, const ecs::task& tk)
			{
				auto o = operation{ paramList, pass, tk };
				auto index = o.get_index();
				auto hds = o.get_parameter<Heading>();
				auto trs = o.get_parameter_owned<Translation>();
				auto boid = o.get_parameter<const Boid>(); //这玩意是 shared
				forloop(i, 0, o.get_count())
				{
					hds[i] = (*newHeadings)[i + index];
					trs[i] += hds[i] * deltaTime * boid->MoveSpeed;
				}
			});
	}
}

int main()
{
	if (!IModule::Registry::regist("ECS", &ECSModule::create) || !sakura::IModule::StartUp("ECS"))
	{
		sakura::error("Failed to StartUp ECSModule!");
		return -1;
	}

	using namespace sakura::ecs;

	cid<Translation> = register_component<Translation>();
	cid<Rotation> = register_component<Rotation>();
	cid<RotationEuler> = register_component<RotationEuler>();
	cid<Scale> = register_component<Scale>();
	cid<LocalToWorld> = register_component<LocalToWorld>();
	cid<LocalToParent> = register_component<LocalToParent>();
	cid<WorldToLocal> = register_component<WorldToLocal>();
	cid<Child> = register_component<Child>();
	cid<Parent> = register_component<Parent>();
	
	entity_type type = {
		complist<Translation, RotationEuler, Rotation, Scale, LocalToWorld, WorldToLocal> }; 
	{
		for (auto c : ctx.allocate(type, 2000000))
		{
			RotationEuler::value_type* rotators = init_component<RotationEuler>(ctx, c);
			Scale::value_type* scales = init_component<Scale>(ctx, c);
			Translation::value_type* translations = init_component<Translation>(ctx, c);
			auto l2ws = init_component<LocalToWorld>(ctx, c);
			const entity* ents = ctx.get_entities(c.c);
			auto ent_p = ents[0];
			auto ent_c = ents[c.count - 1];
			
			forloop(i, 0, c.count)
			{
				rotators[i] = Rotator{ 0.f, 0.f, 0.f };
				scales[i] = Vector3f::vector_one();
				translations[i] = Vector3f(std::array<float, 3>{1.f, 2.f, 3.f});
				l2ws[i] = float4x4();
			}

			forloop(i, 0, c.count)
			{
				chunk_slice cs;
				cs.c = c.c;
				cs.count = 1;
				cs.start = c.start;
				if (i == 0)
				{
					type_diff diff;
					index_t extend[] = { cid<Child> };
					diff.extend = entity_type{ {extend} };
					ctx.cast(cs, diff);

					auto p = (value_type_t<Child>)ctx.get_owned_rw(ent_p, cid<Child>);
					p.push(ent_c);
				}
				else if (i == 1)
				{
					type_diff diff;
					index_t extend[] = { cid<Parent>, cid<LocalToParent> };
					diff.extend = entity_type{ {extend} };
					ctx.cast(cs, diff);

					auto p = (value_type_t<Parent>)ctx.get_owned_rw(ent_c, cid<Parent>);
					auto l2p = (value_type_t<LocalToParent>)ctx.get_owned_rw(ent_c, cid<LocalToParent>);
					auto l2w = (value_type_t<LocalToWorld>)ctx.get_owned_rw(ent_c, cid<LocalToWorld>);
					*l2w = float4x4();
					*l2p = float4x4();
					*p = ent_p;
				}
			}
		}
	}
	
	task_system::Scheduler scheduler(task_system::Scheduler::Config::allCores());
	scheduler.bind();
	defer(scheduler.unbind());  // Automatically unbind before returning.


	

	while(1)
	{
		task_system::ecs::pipeline transform_pipeline(ctx);
		transform_pipeline.on_sync = [&](pass** dependencies, int dependencyCount)
		{
			forloop(i, 0, dependencyCount)
				transform_pipeline.pass_events[dependencies[i]->passIndex].wait();
		};
		auto rotationEulerSystem = RotationEulerSystem(transform_pipeline);

		filters wrd_filter;
		wrd_filter.archetypeFilter = {
			{complist<LocalToWorld>},
			{complist<Translation, Scale, Rotation>},
			{complist<LocalToParent, Parent>}
		};
		auto parentWorldSystem = Local2XSystem<LocalToWorld>(transform_pipeline, wrd_filter);

		filters c2p_filter;
		c2p_filter.archetypeFilter = {
			{complist<LocalToParent, Parent>},
			{complist<Translation, Scale, Rotation>},
			{}
		};
		auto child2ParentSystem = Local2XSystem<LocalToParent>(transform_pipeline, c2p_filter);

		auto child2WorldSystem = Child2WorldSystem(transform_pipeline);

		auto world2LocalSystem = World2LocalSystem(transform_pipeline);
		
		// 等待pass
		rotationEulerSystem.wait();
		std::cout << "RotationEulerSystem Finished!" << std::endl;
		parentWorldSystem.wait();
		std::cout << "ParentWorldSystem Finished!" << std::endl;
		child2ParentSystem.wait();
		std::cout << "Child2ParentSystem Finished!" << std::endl;
		child2WorldSystem.wait();
		std::cout << "Child2WorldSystem Finished!" << std::endl;
		world2LocalSystem.wait();
		std::cout << "World2LocalSystem Finished!" << std::endl;
		
		// 等待pipeline
		transform_pipeline.wait();
	}
}