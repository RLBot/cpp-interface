#include "Simulator.h"

#ifdef _WIN32
#include <WsaData.h>
#endif

#include <cstdlib>

int main ()
{
	auto const meshPath = std::getenv ("RS_COLLISION_MESHES");
	RocketSim::Init (meshPath ? meshPath : "collision_meshes");

#ifdef _WIN32
	rlbot::detail::WsaData wsaData;
	if (!wsaData.init ())
		return EXIT_FAILURE;
#endif

	auto simulator = Simulator::create ();
	if (!simulator)
		return EXIT_FAILURE;

	if (!simulator->run ())
		return EXIT_FAILURE;
}
