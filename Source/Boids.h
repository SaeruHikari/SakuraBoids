#include <set>
#include "ECS/ECS.h"
#include "Math/Math.hpp"

using namespace core::guid_parse::literals;
namespace ecs = sakura::ecs;

struct Boid
{
	float SeparationWeight;
	float AlignmentWeight;
	float TargetWeight;
	float ObstacleAversionDistance;
	float MoveSpeed;

	static constexpr auto guid = "A8C09FA6-F29C-4477-9B90-75F8F5490380"_guid;
};