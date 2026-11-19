#include "Config.h"

namespace
{
	std::string Trim(std::string a_str)
	{
		auto notSpace = [](unsigned char c) { return !std::isspace(c); };
		a_str.erase(a_str.begin(), std::find_if(a_str.begin(), a_str.end(), notSpace));
		a_str.erase(std::find_if(a_str.rbegin(), a_str.rend(), notSpace).base(), a_str.end());
		return a_str;
	}

	std::string Lower(std::string a_str)
	{
		std::ranges::transform(a_str, a_str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_str;
	}

	bool ParseBool(const std::string& a_val)
	{
		const auto v = Lower(a_val);
		return v == "1" || v == "true" || v == "yes" || v == "on";
	}

	std::vector<std::string> SplitList(const std::string& a_val)
	{
		std::vector<std::string> out;
		std::string               cur;
		for (char c : a_val) {
			if (c == ',') {
				if (auto t = Trim(cur); !t.empty()) {
					out.push_back(std::move(t));
				}
				cur.clear();
			} else {
				cur.push_back(c);
			}
		}
		if (auto t = Trim(cur); !t.empty()) {
			out.push_back(std::move(t));
		}
		return out;
	}
}

void Config::Load()
{
	std::ifstream in("Data/SKSE/Plugins/PlantAnything.ini");
	if (!in) {
		spdlog::info("PlantAnything.ini not found - using defaults");
		return;
	}

	std::string line;
	while (std::getline(in, line)) {
		if (const auto c = line.find_first_of(";#"); c != std::string::npos) {
			line.erase(c);
		}
		line = Trim(line);
		if (line.empty() || line.front() == '[') {
			continue;
		}
		const auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const auto key = Lower(Trim(line.substr(0, eq)));
		const auto val = Trim(line.substr(eq + 1));

		try {
			if (key == "bpatchfertilesoil") patchFertileSoil = ParseBool(val);
			else if (key == "benabledropplanting") enableDropPlanting = ParseBool(val);
			else if (key == "bsoilallingredients") soilAllIngredients = ParseBool(val);
			else if (key == "bliftburiedplants") liftBuriedPlants = ParseBool(val);
			else if (key == "sskipplanterlists") skippedPlanterLists = SplitList(val);
			else if (key == "brequiresneak") requireSneak = ParseBool(val);
			else if (key == "ballowinteriors") allowInteriors = ParseBool(val);
			else if (key == "bincludetrees") includeTrees = ParseBool(val);
			else if (key == "bincludefood") includeFood = ParseBool(val);
			else if (key == "bincludepotions") includePotions = ParseBool(val);
			else if (key == "brandomvariant") randomVariant = ParseBool(val);
			else if (key == "ballowuproot") allowUproot = ParseBool(val);
			else if (key == "bshowmessages") showMessages = ParseBool(val);
			else if (key == "fregrowdays") regrowDays = (std::max)(0.0f, std::stof(val));
			else if (key == "fsproutdelayseconds") sproutDelay = std::clamp(std::stof(val), 0.0f, 30.0f);
			else if (key == "fsinkdepth") sinkDepth = std::stof(val);
			else if (key == "sexcludedplugins") {
				excludedPlugins.clear();
				for (auto& p : SplitList(val)) {
					excludedPlugins.push_back(Lower(p));
				}
			} else if (key == "sexcludeditems") excludedItems = SplitList(val);
		} catch (...) {
			spdlog::warn("Bad value for '{}': '{}'", key, val);
		}
	}

	spdlog::info("Config: fertileSoil={} allIngredients={} liftBuried={} dropPlanting={}",
		patchFertileSoil, soilAllIngredients, liftBuriedPlants, enableDropPlanting);
	spdlog::info("Config: sneak={} interiors={} trees={} food={} potions={} random={} uproot={} regrowDays={} sproutDelay={}",
		requireSneak, allowInteriors, includeTrees, includeFood, includePotions, randomVariant, allowUproot, regrowDays, sproutDelay);
}

bool Config::IsPluginExcluded(std::string_view a_fileName)
{
	if (excludedPlugins.empty()) {
		return false;
	}
	const auto name = Lower(std::string(a_fileName));
	return std::ranges::find(excludedPlugins, name) != excludedPlugins.end();
}

std::unordered_set<RE::FormID> Config::Resolve(const std::vector<std::string>& a_entries, std::string_view a_key)
{
	std::unordered_set<RE::FormID> out;
	auto*                          dh = RE::TESDataHandler::GetSingleton();
	if (!dh) {
		return out;
	}

	for (const auto& entry : a_entries) {
		const auto bar = entry.find('|');
		if (bar == std::string::npos) {
			spdlog::warn("{} entry '{}' should look like Plugin.esp|0x123456", a_key, entry);
			continue;
		}
		const auto plugin = Trim(entry.substr(0, bar));
		const auto idStr = Trim(entry.substr(bar + 1));
		try {
			const auto localID = static_cast<RE::FormID>(std::stoul(idStr, nullptr, 16));
			if (auto* form = dh->LookupForm(localID, plugin)) {
				out.insert(form->GetFormID());
			} else {
				spdlog::warn("{}: {} not found (plugin not loaded?)", a_key, entry);
			}
		} catch (...) {
			spdlog::warn("{}: bad FormID in '{}'", a_key, entry);
		}
	}
	return out;
}
