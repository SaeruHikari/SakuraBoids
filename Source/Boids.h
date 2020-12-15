#include <set>
#include "ECS/ECS.h"
#include "Math/Math.hpp"

using namespace core::guid_parse::literals;
namespace ecs = sakura::ecs;

struct Boid
{
	float SightRadius;
	float SeparationWeight;
	float AlignmentWeight;
	float TargetWeight;
	float MoveSpeed;

	static constexpr auto guid = "A8C09FA6-F29C-4477-9B90-75F8F5490380"_guid;
};

struct BoidTarget
{
	static constexpr auto guid = "86EA5D88-3885-469B-BE73-5DB1B20BD817"_guid;
};

struct Heading
{
	using value_type = sakura::Vector3f;
	static constexpr auto guid = "BA82CDD4-56B8-40C5-BD5B-7AD8CFF867C4"_guid;
	sakura::Vector3f value;
};

struct MoveToward
{
	sakura::Vector3f Target;
	float MoveSpeed;
};

struct RandomMoveTarget
{
	sakura::Vector3f Center;
	float Radius; 
};