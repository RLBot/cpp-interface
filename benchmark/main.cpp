#include "Simulator.h"

#include <cstdlib>

int main ()
{
	auto const meshPath = std::getenv ("RS_COLLISION_MESHES");
	RocketSim::Init (meshPath ? meshPath : "collision_meshes");

	auto simulator = Simulator::create ();
	if (!simulator || !simulator->run ())
		return EXIT_FAILURE;
}
