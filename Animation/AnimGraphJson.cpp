#include "Animation/AnimGraphJson.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <cstdint>
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
}
