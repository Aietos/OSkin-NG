-- OSkin's config model. It writes OSkin_skinDistributionConfig_schema.json (draft 4, which rapidjson reads) and
-- OSkin_skinDistributionConfig.json with every key at its default.
--
-- Run it with xmake from the project root: xmake schema
-- Or directly: xmake lua Data/SKSE/Plugins/lua/OSkinConfigModel.lua
--
-- Plain Lua apart from finding its own folder, so any Lua 5.x interpreter can also run it.

-- JSON objects keep their key order, so they're written as a list of {key, value} pairs
local function object(pairs_)
    return {__object = pairs_ or {}}
end

local function list(items)
    return {__list = items or {}}
end

local function escape(text)
    local replaced = text:gsub('[%c"\\]', function(c)
        local named = {['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t'}

        return named[c] or string.format("\\u%04x", c:byte())
    end)

    return '"' .. replaced .. '"'
end

local function encode(value, indent)
    local pad = string.rep("  ", indent)
    local inner = string.rep("  ", indent + 1)
    local kind = type(value)

    if kind == "string" then return escape(value) end

    if kind == "boolean" then return tostring(value) end

    if kind == "number" then return tostring(value) end

    if value.__list then
        if #value.__list == 0 then return "[]" end

        local lines = {}

        for _, item in ipairs(value.__list) do
            table.insert(lines, inner .. encode(item, indent + 1))
        end

        return "[\n" .. table.concat(lines, ",\n") .. "\n" .. pad .. "]"
    end

    if #value.__object == 0 then return "{}" end

    local lines = {}

    for _, member in ipairs(value.__object) do
        table.insert(lines, inner .. escape(member[1]) .. ": " .. encode(member[2], indent + 1))
    end

    return "{\n" .. table.concat(lines, ",\n") .. "\n" .. pad .. "}"
end

local function ref(name)
    return object({{"$ref", "#/definitions/" .. name}})
end

-- Types shared by the keys
local parts = {"diffuse", "normal", "subsurface", "specular", "head", "headNormal", "teeth"}

local types = {
    {"NonEmptyTrimmedString", object({{"pattern", [[^(?=\S)(?=.*\S$).*]]}, {"type", "string"}})},
    {"NonEmptyString", object({{"pattern", [[^(?!\s*$).+]]}, {"type", "string"}})},
    {"BSTFile", ref("NonEmptyTrimmedString")},
    {"FormID", object({{"pattern", "^[0-9A-Fa-f]{3,8}$"}, {"type", "string"}})},
    {"EditorID", ref("NonEmptyTrimmedString")},
    {"RaceName", ref("NonEmptyTrimmedString")},
    {"ClassName", ref("NonEmptyTrimmedString")},
    {"SkinName", ref("NonEmptyString")},
    {"SkinMix", object({
        {"additionalProperties", false},
        {"properties", object({
            {"base", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack everything not named below comes from."}})},
            {"diffuse", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack the color maps of the whole body come from."}})},
            {"normal", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack the normal maps of the whole body come from."}})},
            {"subsurface", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack the subsurface (skin tint) maps of the whole body come from."}})},
            {"specular", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack the specular maps of the whole body come from."}})},
            {"head", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack the head takes its files from first. Without it, the head follows the body."}})},
            {"headNormal", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack the head's normal map comes from. It comes before head."}})},
            {"teeth", object({{"$ref", "#/definitions/SkinName"}, {"description", "The pack the mouth textures (actors\\character\\mouth) come from."}})},
        })},
        {"required", list({"base"})},
        {"type", "object"},
    })},
    {"SkinRule", object({
        {"additionalProperties", false},
        {"properties", object({
            {"parts", object({
                {"description", "The parts of the skin the rule sets: diffuse, normal, subsurface and specular for the whole body, head, headNormal, teeth. Without it, the whole skin, with the pack's other files (face detail maps, elders' maps)."},
                {"items", object({{"enum", list(parts)}, {"type", "string"}})},
                {"minItems", 1},
                {"type", "array"},
            })},
            {"sex", object({{"description", "Only NPCs of this sex. Without it, both."}, {"enum", list({"female", "male"})}, {"type", "string"}})},
            {"race", object({{"description", "Only NPCs of these races."}, {"items", ref("RaceName")}, {"type", "array"}})},
            {"class", object({{"description", "Only NPCs of these classes."}, {"items", ref("ClassName")}, {"type", "array"}})},
            {"faction", object({{"description", "Only NPCs in one of these factions."}, {"items", ref("EditorID")}, {"type", "array"}})},
            {"plugin", object({{"description", "Only NPCs added or changed by one of these plugins."}, {"items", ref("BSTFile")}, {"type", "array"}})},
            {"npcFormID", object({
                {"additionalProperties", object({{"items", ref("FormID")}, {"type", "array"}})},
                {"description", "Only these NPCs, by plugin and FormID."},
                {"propertyNames", ref("BSTFile")},
                {"type", "object"},
            })},
            {"weight", object({
                {"description", "Only NPCs whose weight is in [min, max], from 0 to 100, both ends included."},
                {"items", object({{"maximum", 100}, {"minimum", 0}, {"type", "number"}})},
                {"maxItems", 2},
                {"minItems", 2},
                {"type", "array"},
            })},
            {"vampire", object({{"description", "true: only vampires. false: no vampires. Without it, both."}, {"type", "boolean"}})},
            {"obodyPreset", object({{"description", "Only NPCs whose OBody preset is one of these BodySlide presets, by the name in the preset file. Needs OBody NG."}, {"items", ref("NonEmptyString")}, {"type", "array"}})},
            {"skins", object({{"description", "The packs or mixes the rule gives, one per NPC. \"*\" is every skin with body color maps for the NPC's sex."}, {"items", ref("SkinName")}, {"minItems", 1}, {"type", "array"}})},
            {"exceptSkins", object({{"description", "The skins \"*\" leaves out."}, {"items", ref("SkinName")}, {"type", "array"}})},
            {"gameSkin", object({{"description", "true: the parts the rule sets keep the game's textures, in place of skins."}, {"type", "boolean"}})},
            {"off", object({{"description", "true: the rule is switched off."}, {"type", "boolean"}})},
        })},
        {"anyOf", list({object({{"required", list({"skins"})}}), object({{"required", list({"gameSkin"})}})})},
        {"type", "object"},
    })},
}

-- The config's keys, in the order the default config is written
local keys = {
    {"skinRules", object({
        {"additionalProperties", ref("SkinRule")},
        {"default", object()},
        {"description", "Named rules, read from top to bottom: each part of an NPC's skin comes from the first rule that fits the NPC and sets that part. Parts no rule sets come from a random skin. A skin picked in the OSkin menu replaces the rules for that NPC."},
        {"propertyNames", ref("NonEmptyTrimmedString")},
        {"type", "object"},
    }), object()},
    {"blacklistedSkinsFromRandomDistribution", object({
        {"default", list()},
        {"description", "The packs or mixes random skins never give. Rules can still name them."},
        {"items", ref("SkinName")},
        {"type", "array"},
    }), list()},
    {"blacklistedSkinsShowInMenu", object({
        {"default", true},
        {"description", "Whether the skins left out of random skins, or of a rule's \"*\" by exceptSkins, still show in the Skins tab, to be picked by hand."},
        {"type", "boolean"},
    }), true},
    {"skinMixes", object({
        {"additionalProperties", ref("SkinMix")},
        {"default", object()},
        {"description", "Named mixes of packs. A mix takes each part it names from that pack, and everything else from its base. A mix can't use a pack's name. Wherever a skin name goes, it can be a pack or a mix."},
        {"propertyNames", ref("SkinName")},
        {"type", "object"},
    }), object()},
    {"menuHotkey", object({
        {"default", 37},
        {"description", "The key that opens the OSkin menu, as a DirectX scan code (37 is K). 0 is no key, the menu still opens from SKSE Menu Framework."},
        {"minimum", 0},
        {"type", "integer"},
    }), 37},
    {"disableSkinDistribution", object({
        {"default", "none"},
        {"description", "Turns the rules off for a sex, or for everyone. Valid values: \"none\", \"male\", \"female\", \"all\". Skins picked in the menu stay."},
        {"enum", list({"none", "male", "female", "all"})},
        {"type", "string"},
    }), "none"},
    {"playerFollowsRules", object({
        {"default", false},
        {"description", "true: the player gets skins from the rules like an NPC, unless one was picked in the menu. false: only rules that name the player (Skyrim.esm 00000007 in npcFormID) apply to them."},
        {"type", "boolean"},
    }), false},
}

local function buildSchema()
    local properties = {}
    local definitions = {}

    for _, key in ipairs(keys) do
        table.insert(properties, {key[1], ref(key[1])})
    end

    for _, entry in ipairs(types) do
        table.insert(definitions, entry)
    end

    for _, key in ipairs(keys) do
        table.insert(definitions, {key[1], key[2]})
    end

    return object({
        {"additionalProperties", false},
        {"properties", object(properties)},
        {"title", "OSkinConfigModel"},
        {"type", "object"},
        {"definitions", object(definitions)},
    })
end

local function buildDefaults()
    local members = {}

    for _, key in ipairs(keys) do
        table.insert(members, {key[1], key[3]})
    end

    return object(members)
end

local function writeFile(path, text)
    local file = assert(io.open(path, "wb"))

    file:write(text)
    file:close()
    print("Wrote " .. path)
end

local function pluginsFolder()
    if os.scriptdir then
        return path.directory(os.scriptdir())
    end

    local source = debug.getinfo(1, "S").source:gsub("^@", "")

    return (source:match("^(.*)[/\\][^/\\]*$") or ".") .. "/.."
end

function main()
    local folder = pluginsFolder()

    writeFile(folder .. "/OSkin_skinDistributionConfig_schema.json", encode(buildSchema(), 0) .. "\n")
    writeFile(folder .. "/OSkin_skinDistributionConfig.json", encode(buildDefaults(), 0) .. "\n")
end

if not xmake then
    main()
end
