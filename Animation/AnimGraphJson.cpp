#include "Animation/AnimGraphJson.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace Dark
{
	using json = nlohmann::json;

	namespace
	{
		int32_t findStateIndex(const std::vector<AnimStateDef>& states, std::string_view name)
		{
			for (uint32_t i = 0; i < states.size(); ++i)
			{
				if (states[i].name == name)
					return static_cast<int32_t>(i);
			}
			return -1;
		}

		int32_t findParamIndex(const std::vector<AnimParamDef>& params, std::string_view name)
		{
			for (uint32_t i = 0; i < params.size(); ++i)
			{
				if (params[i].name == name)
					return static_cast<int32_t>(i);
			}
			return -1;
		}

		bool parseCondition(const json& jc, const std::vector<AnimParamDef>& params, AnimCondition& out)
		{
			if (!jc.is_object() || !jc.contains("param") || !jc["param"].is_string())
			{
				DE_LOG_ERROR("AnimGraph: condition missing param name");
				return false;
			}
			const std::string pname = jc["param"].get<std::string>();
			const int32_t pi = findParamIndex(params, pname);
			if (pi < 0)
			{
				DE_LOG_ERROR("AnimGraph: unknown condition param '{}'", pname);
				return false;
			}
			out.paramIndex = static_cast<uint32_t>(pi);

			struct OpKey
			{
				const char*         key;
				AnimCondition::Op   op;
			};
			static const OpKey kOps[] = {
				{ "gt", AnimCondition::Op::Gt },
				{ "lt", AnimCondition::Op::Lt },
				{ "ge", AnimCondition::Op::Ge },
				{ "le", AnimCondition::Op::Le },
				{ "eq", AnimCondition::Op::Eq },
				{ "ne", AnimCondition::Op::Ne },
			};
			bool found = false;
			for (const OpKey& k : kOps)
			{
				if (!jc.contains(k.key))
					continue;
				out.op = k.op;
				const json& val = jc[k.key];
				if (val.is_boolean())
					out.boolValue = val.get<bool>();
				else if (val.is_number())
					out.floatValue = val.get<float>();
				else
				{
					DE_LOG_ERROR("AnimGraph: condition '{}' has invalid value", k.key);
					return false;
				}
				found = true;
				break;
			}
			if (!found)
			{
				DE_LOG_ERROR("AnimGraph: condition '{}' has no operator", pname);
				return false;
			}
			return true;
		}
	} // namespace

	bool parseAnimGraphJson(const char* jsonText, const AnimationSet& animSet, AnimGraphDef& out)
	{
		out.defaultState = 0;
		out.modelPath.clear();
		out.params.clear();
		out.states.clear();
		out.transitions.clear();
		out.overlayMarkers.clear();
		out.overlayClipIndex.clear();
		if (!jsonText || jsonText[0] == '\0')
		{
			DE_LOG_ERROR("AnimGraph: empty json");
			return false;
		}

		const json root = json::parse(jsonText, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			DE_LOG_ERROR("AnimGraph: discarded or non-object json");
			return false;
		}

		if (root.contains("version"))
		{
			if (!root["version"].is_number_integer() || root["version"].get<int>() != 1)
			{
				DE_LOG_ERROR("AnimGraph: unsupported version");
				return false;
			}
		}

		if (root.contains("model") && root["model"].is_string())
			out.modelPath = root["model"].get<std::string>();

		if (root.contains("parameters") && root["parameters"].is_array())
		{
			for (const json& jp : root["parameters"])
			{
				if (!jp.is_object() || !jp.contains("name") || !jp["name"].is_string())
				{
					DE_LOG_ERROR("AnimGraph: parameter missing name");
					return false;
				}
				AnimParamDef p;
				p.name = jp["name"].get<std::string>();
				std::string type = "float";
				if (jp.contains("type") && jp["type"].is_string())
					type = jp["type"].get<std::string>();
				if (type == "bool")
					p.type = AnimParamType::Bool;
				else if (type == "trigger")
					p.type = AnimParamType::Trigger;
				else
					p.type = AnimParamType::Float;
				if (jp.contains("default"))
				{
					if (jp["default"].is_number())
						p.defaultFloat = jp["default"].get<float>();
					else if (jp["default"].is_boolean())
						p.defaultBool = jp["default"].get<bool>();
				}
				out.params.push_back(std::move(p));
			}
		}

		if (!root.contains("states") || !root["states"].is_array() || root["states"].empty())
		{
			DE_LOG_ERROR("AnimGraph: states array required");
			return false;
		}
		for (const json& js : root["states"])
		{
			if (!js.is_object() || !js.contains("name") || !js["name"].is_string() || !js.contains("clip") || !js["clip"].is_string())
			{
				DE_LOG_ERROR("AnimGraph: state requires name and clip");
				return false;
			}
			AnimStateDef st;
			st.name = js["name"].get<std::string>();
			const std::string clipName = js["clip"].get<std::string>();
			const int32_t ci = animSet.findClipIndex(clipName);
			if (ci < 0)
			{
				DE_LOG_ERROR("AnimGraph: unknown clip '{}' in state '{}'", clipName, st.name);
				return false;
			}
			st.clipIndex = static_cast<uint32_t>(ci);
			if (js.contains("speed") && js["speed"].is_number())
				st.speed = js["speed"].get<float>();
			if (js.contains("loop") && js["loop"].is_boolean())
				st.loop = js["loop"].get<bool>();
			out.states.push_back(std::move(st));
		}

		if (!root.contains("defaultState") || !root["defaultState"].is_string())
		{
			DE_LOG_ERROR("AnimGraph: defaultState required");
			return false;
		}
		{
			const std::string defName = root["defaultState"].get<std::string>();
			const int32_t si = findStateIndex(out.states, defName);
			if (si < 0)
			{
				DE_LOG_ERROR("AnimGraph: unknown defaultState '{}'", defName);
				return false;
			}
			out.defaultState = static_cast<uint32_t>(si);
		}

		if (root.contains("transitions") && root["transitions"].is_array())
		{
			for (const json& jt : root["transitions"])
			{
				if (!jt.is_object() || !jt.contains("from") || !jt["from"].is_string() || !jt.contains("to") || !jt["to"].is_string())
				{
					DE_LOG_ERROR("AnimGraph: transition requires from and to");
					return false;
				}
				AnimTransitionDef tr;
				const std::string fromName = jt["from"].get<std::string>();
				const std::string toName = jt["to"].get<std::string>();
				if (fromName == "*" || fromName == "Any")
					tr.from = kAnyState;
				else
				{
					const int32_t fi = findStateIndex(out.states, fromName);
					if (fi < 0)
					{
						DE_LOG_ERROR("AnimGraph: unknown transition from '{}'", fromName);
						return false;
					}
					tr.from = static_cast<uint32_t>(fi);
				}
				const int32_t ti = findStateIndex(out.states, toName);
				if (ti < 0)
				{
					DE_LOG_ERROR("AnimGraph: unknown transition to '{}'", toName);
					return false;
				}
				tr.to = static_cast<uint32_t>(ti);
				if (jt.contains("blend") && jt["blend"].is_number())
					tr.blendSec = jt["blend"].get<float>();
				if (jt.contains("interrupt") && jt["interrupt"].is_boolean())
					tr.canInterrupt = jt["interrupt"].get<bool>();
				if (jt.contains("onClipEnd") && jt["onClipEnd"].is_boolean())
					tr.onClipEnd = jt["onClipEnd"].get<bool>();
				if (jt.contains("when") && jt["when"].is_array())
				{
					for (const json& jc : jt["when"])
					{
						AnimCondition c;
						if (!parseCondition(jc, out.params, c))
							return false;
						tr.when.push_back(c);
					}
				}
				out.transitions.push_back(std::move(tr));
			}
		}

		if (root.contains("notifies") && root["notifies"].is_array())
		{
			if (root["notifies"].size() > 64)
			{
				DE_LOG_ERROR("AnimGraph: sidecar notifies exceed 64");
				return false;
			}
			for (const json& jn : root["notifies"])
			{
				if (!jn.is_object() || !jn.contains("clip") || !jn["clip"].is_string() || !jn.contains("name") || !jn["name"].is_string())
				{
					DE_LOG_ERROR("AnimGraph: notify requires clip and name");
					return false;
				}
				const std::string clipName = jn["clip"].get<std::string>();
				const int32_t ci = animSet.findClipIndex(clipName);
				if (ci < 0)
				{
					DE_LOG_ERROR("AnimGraph: unknown notify clip '{}'", clipName);
					return false;
				}
				AnimMarker m;
				m.name = jn["name"].get<std::string>();
				if (jn.contains("time") && jn["time"].is_number())
					m.time = jn["time"].get<float>();
				else if (jn.contains("frame") && jn["frame"].is_number())
				{
					float fps = 30.0f;
					if (jn.contains("fps") && jn["fps"].is_number())
						fps = jn["fps"].get<float>();
					if (fps <= 0.0f)
						fps = 30.0f;
					m.time = jn["frame"].get<float>() / fps;
				}
				if (jn.contains("int") && jn["int"].is_number_integer())
					m.intPayload = jn["int"].get<int32_t>();
				if (jn.contains("float") && jn["float"].is_number())
					m.floatPayload = jn["float"].get<float>();
				const AnimationClip* clip = animSet.clipAt(static_cast<uint32_t>(ci));
				if (clip && clip->duration > 0.0f)
				{
					if (m.time < 0.0f)
						m.time = 0.0f;
					if (m.time > clip->duration)
						m.time = clip->duration;
				}
				out.overlayMarkers.push_back(std::move(m));
				out.overlayClipIndex.push_back(static_cast<uint32_t>(ci));
			}
		}

		return !out.states.empty();
	}

	bool peekAnimGraphModelPath(const char* jsonText, std::string& outModelPath)
	{
		outModelPath.clear();
		if (!jsonText || jsonText[0] == '\0')
			return false;
		const json root = json::parse(jsonText, nullptr, false);
		if (root.is_discarded() || !root.is_object())
			return false;
		if (!root.contains("model") || !root["model"].is_string())
			return false;
		outModelPath = root["model"].get<std::string>();
		return !outModelPath.empty();
	}

	bool initAnimGraphFromSet(AnimGraphDef& out, const AnimationSet& animSet, std::string_view modelPath)
	{
		out.defaultState = 0;
		out.modelPath = std::string(modelPath);
		out.params.clear();
		out.states.clear();
		out.transitions.clear();
		out.overlayMarkers.clear();
		out.overlayClipIndex.clear();
		if (animSet.clipCount() == 0)
		{
			DE_LOG_ERROR("AnimGraph: cannot init from empty AnimationSet");
			return false;
		}
		for (uint32_t i = 0; i < animSet.clipCount(); ++i)
		{
			const AnimationClip* clip = animSet.clipAt(i);
			if (!clip)
				return false;
			AnimStateDef st;
			st.name = clip->name.empty() ? ("Clip" + std::to_string(i)) : clip->name;
			st.clipIndex = i;
			st.loop = clip->loopDefault;
			out.states.push_back(std::move(st));
		}
		return true;
	}

	bool writeAnimGraphJson(const AnimGraphDef& def, std::string& outText)
	{
		outText.clear();
		if (!def.animSet || def.states.empty())
		{
			DE_LOG_ERROR("AnimGraph: cannot write graph with no states");
			return false;
		}
		if (def.defaultState >= def.states.size())
		{
			DE_LOG_ERROR("AnimGraph: defaultState out of range");
			return false;
		}

		using ojson = nlohmann::ordered_json;
		ojson root = ojson::object();
		root["version"] = 1;
		if (!def.modelPath.empty())
			root["model"] = def.modelPath;
		root["defaultState"] = def.states[def.defaultState].name;

		if (!def.params.empty())
		{
			ojson params = ojson::array();
			for (const AnimParamDef& p : def.params)
			{
				ojson jp = ojson::object();
				jp["name"] = p.name;
				if (p.type == AnimParamType::Bool)
				{
					jp["type"] = "bool";
					jp["default"] = p.defaultBool;
				}
				else if (p.type == AnimParamType::Trigger)
				{
					jp["type"] = "trigger";
				}
				else
				{
					jp["type"] = "float";
					jp["default"] = p.defaultFloat;
				}
				params.push_back(std::move(jp));
			}
			root["parameters"] = std::move(params);
		}

		ojson states = ojson::array();
		for (const AnimStateDef& st : def.states)
		{
			const AnimationClip* clip = def.animSet->clipAt(st.clipIndex);
			if (!clip)
			{
				DE_LOG_ERROR("AnimGraph: state '{}' has invalid clip index", st.name);
				return false;
			}
			ojson js = ojson::object();
			js["name"] = st.name;
			js["clip"] = clip->name;
			js["loop"] = st.loop;
			if (st.speed != 1.0f)
				js["speed"] = st.speed;
			states.push_back(std::move(js));
		}
		root["states"] = std::move(states);

		if (!def.transitions.empty())
		{
			ojson transitions = ojson::array();
			for (const AnimTransitionDef& t : def.transitions)
			{
				if (t.to >= def.states.size())
				{
					DE_LOG_ERROR("AnimGraph: transition to index out of range");
					return false;
				}
				if (t.from != kAnyState && t.from >= def.states.size())
				{
					DE_LOG_ERROR("AnimGraph: transition from index out of range");
					return false;
				}
				ojson jt = ojson::object();
				jt["from"] = (t.from == kAnyState) ? "*" : def.states[t.from].name;
				jt["to"] = def.states[t.to].name;
				jt["blend"] = t.blendSec;
				if (t.canInterrupt)
					jt["interrupt"] = true;
				else
					jt["interrupt"] = false;
				if (t.onClipEnd)
					jt["onClipEnd"] = true;
				if (!t.when.empty())
				{
					ojson when = ojson::array();
					for (const AnimCondition& c : t.when)
					{
						if (c.paramIndex >= def.params.size())
						{
							DE_LOG_ERROR("AnimGraph: condition param index out of range");
							return false;
						}
						ojson jc = ojson::object();
						jc["param"] = def.params[c.paramIndex].name;
						const char* opKey = "gt";
						switch (c.op)
						{
						case AnimCondition::Op::Lt: opKey = "lt"; break;
						case AnimCondition::Op::Ge: opKey = "ge"; break;
						case AnimCondition::Op::Le: opKey = "le"; break;
						case AnimCondition::Op::Eq: opKey = "eq"; break;
						case AnimCondition::Op::Ne: opKey = "ne"; break;
						case AnimCondition::Op::Gt:
						default: opKey = "gt"; break;
						}
						if (def.params[c.paramIndex].type == AnimParamType::Float)
							jc[opKey] = c.floatValue;
						else
							jc[opKey] = c.boolValue;
						when.push_back(std::move(jc));
					}
					jt["when"] = std::move(when);
				}
				transitions.push_back(std::move(jt));
			}
			root["transitions"] = std::move(transitions);
		}

		if (!def.overlayMarkers.empty())
		{
			ojson notifies = ojson::array();
			const uint32_t count = static_cast<uint32_t>(
				def.overlayMarkers.size() < def.overlayClipIndex.size() ? def.overlayMarkers.size() : def.overlayClipIndex.size());
			for (uint32_t i = 0; i < count; ++i)
			{
				const AnimationClip* clip = def.animSet->clipAt(def.overlayClipIndex[i]);
				if (!clip)
				{
					DE_LOG_ERROR("AnimGraph: notify clip index out of range");
					return false;
				}
				ojson jn = ojson::object();
				jn["clip"] = clip->name;
				jn["name"] = def.overlayMarkers[i].name;
				jn["time"] = def.overlayMarkers[i].time;
				if (def.overlayMarkers[i].intPayload != 0)
					jn["int"] = def.overlayMarkers[i].intPayload;
				if (def.overlayMarkers[i].floatPayload != 0.0f)
					jn["float"] = def.overlayMarkers[i].floatPayload;
				notifies.push_back(std::move(jn));
			}
			root["notifies"] = std::move(notifies);
		}

		outText = root.dump(2);
		outText += '\n';
		return true;
	}

	bool saveAnimGraphJsonFile(const AnimGraphDef& def, const std::filesystem::path& path)
	{
		std::string text;
		if (!writeAnimGraphJson(def, text))
			return false;
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			DE_LOG_ERROR("AnimGraph: cannot write '{}'", path.string());
			return false;
		}
		out << text;
		if (!out)
		{
			DE_LOG_ERROR("AnimGraph: write failed '{}'", path.string());
			return false;
		}
		DE_LOG_INFO("AnimGraph: saved '{}'", path.string());
		return true;
	}
}
