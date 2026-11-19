#include "Config.h"
#include "Events.h"
#include "Fixups.h"
#include "Garden.h"
#include "PlantRegistry.h"
#include "Soil.h"
#include "SoilPlants.h"

// One DLL for SE 1.5.97 and AE (Address Library). Also exports SKSEPlugin_Query for SE.
SKSEPluginInfo(
	.Version = REL::Version{ 1, 4, 0, 0 },
	.Name = "PlantAnything"sv,
	.Author = "PlantAnything"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary);

namespace
{
	void InitLogger()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "PlantAnything.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("PlantAnything", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			PlantRegistry::GetSingleton()->Build();
			SoilPlants::Init();
			Fixups::Register();
			if (Config::enableDropPlanting) {
				Events::Register();
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			// Script-added FormList entries come back from the save here, so the soil
			// lists can only be lined up now. Safe to repeat on every load.
			spdlog::info("--- Game loaded: patching fertile soil ---");
			SoilPatcher::Run();
			break;
		default:
			break;
		}
	}

	void SaveCallback(SKSE::SerializationInterface* a_intfc)
	{
		Garden::GetSingleton()->Save(a_intfc);
		Fixups::Save(a_intfc);
	}

	void LoadCallback(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			switch (type) {
			case Garden::kRecordType:
				Garden::GetSingleton()->LoadRecord(a_intfc, version);
				break;
			case Fixups::kRecordType:
				Fixups::LoadRecord(a_intfc, version);
				break;
			default:
				break;
			}
		}
	}

	void RevertCallback(SKSE::SerializationInterface*)
	{
		Garden::GetSingleton()->Revert();
		Fixups::Revert();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, SKSE::InitInfo{ .log = false });  // we set up our own PlantAnything.log
	InitLogger();
	spdlog::info("PlantAnything loaded");

	Config::Load();

	if (auto* serialization = SKSE::GetSerializationInterface()) {
		serialization->SetUniqueID('PLNT');
		serialization->SetSaveCallback(SaveCallback);
		serialization->SetLoadCallback(LoadCallback);
		serialization->SetRevertCallback(RevertCallback);
	}

	if (auto* messaging = SKSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(OnMessage)) {
		spdlog::critical("Could not register SKSE message listener");
		return false;
	}
	return true;
}
