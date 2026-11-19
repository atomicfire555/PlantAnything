#pragma once

// Repairs soil plots planted with earlier versions: the plant spawned was a hanging decoration
// (garlic braid, dried herbs...) whose mesh ended up underground. When such a plant comes into
// view it is lifted so its lowest point sits on the soil - visible and harvestable again.
namespace Fixups
{
	static constexpr std::uint32_t kRecordType = 'LIFT';

	void Register();
	void Save(SKSE::SerializationInterface* a_intfc);
	void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
	void Revert();
}
