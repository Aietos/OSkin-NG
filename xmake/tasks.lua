-- Local build task: `xmake dist`.
-- Add `includes("xmake/tasks.lua")` to xmake.lua.
--
-- NOTE: xmake script bodies (on_run) can't see file-level `local`s, so every constant
-- lives inside the function.
--
-- Taken from OBody NG, without the Papyrus step (OSkin has no scripts yet).

-- xmake dist: DLL + package + zip (local equivalent of the GitHub CI) --------
task("dist")
    set_category("plugin")
    set_menu {
        usage = "xmake dist [options]",
        description = "Build the mod locally: build the DLL and create the release zip.",
        options = {
            {"d", "skip-dll", "k", nil, "Don't compile the DLL; package the DLL/PDB already in build/ (if any)."}
        }
    }

    on_run(function ()
        import("core.base.option")

        local PROJECT_NAME = "OSkin"
        local STAGE_DIR    = path.join("contrib", "Package")
        local ARTIFACT_DIR = path.join("contrib", "Artifact")

        local function step(msg) cprint("${bright green}>> ${clear}" .. msg) end

        -- 1. DLL
        if not option.get("skip-dll") then
            step("Building " .. PROJECT_NAME .. ".dll")
            os.execv(os.programfile(), {"-y", PROJECT_NAME})
        end

        -- 2. Stage
        step("Assembling package in " .. STAGE_DIR)
        os.tryrm(STAGE_DIR)
        os.mkdir(STAGE_DIR)

        if not os.isdir("Data") then raise("Data/ folder not found") end
        for _, entry in ipairs(os.filedirs(path.join("Data", "*"))) do
            os.cp(entry, STAGE_DIR)
        end

        local plugins = path.join(STAGE_DIR, "SKSE", "Plugins")
        os.mkdir(plugins)
        for _, name in ipairs({PROJECT_NAME .. ".dll", PROJECT_NAME .. ".pdb"}) do
            local newest, newest_time
            for _, file in ipairs(os.files(path.join("build", "**", name))) do
                local t = os.mtime(file)
                if not newest_time or t > newest_time then newest, newest_time = file, t end
            end
            if newest then
                if option.get("skip-dll") then
                    cprint("${yellow}Reusing existing %s (not rebuilt)", newest)
                end
                os.cp(newest, plugins)
            elseif option.get("skip-dll") then
                cprint("${bright yellow}warning: ${clear}%s not found in build/, packaging without it", name)
            else
                raise("%s was not produced by the build", name)
            end
        end

        for _, junk in ipairs(os.files(path.join(STAGE_DIR, "**", ".git*"))) do os.rm(junk) end

        -- 3. Zip
        local version = io.readfile("xmake.lua"):match('set_version%("([^"]+)"%)')
        if not version then raise("Could not read the version from xmake.lua") end

        os.mkdir(ARTIFACT_DIR)
        local zipfile = path.absolute(path.join(ARTIFACT_DIR, PROJECT_NAME .. "_" .. version .. ".zip"))
        os.tryrm(zipfile)

        -- bsdtar writes .zip with -a. It ships with Windows 10+ (System32\tar.exe); call that
        -- explicitly so a GNU tar earlier in PATH (e.g. from Git) isn't picked up.
        local tar = "tar"
        if is_host("windows") then
            tar = path.join(os.getenv("SystemRoot") or "C:\\Windows", "System32", "tar.exe")
        end

        local entries = {}
        for _, entry in ipairs(os.filedirs(path.join(STAGE_DIR, "*"))) do
            table.insert(entries, path.filename(entry))
        end
        step("Creating " .. zipfile)
        os.execv(tar, table.join({"-a", "-c", "-f", zipfile, "-C", STAGE_DIR}, entries))

        cprint("${bright green}Done:${clear} %s", zipfile)
    end)
task_end()

-- xmake schema: write the config schema and the default config from the Lua config model
task("schema")
    set_category("plugin")
    set_menu {
        usage = "xmake schema",
        description = "Write OSkin_skinDistributionConfig_schema.json and the default config from Data/SKSE/Plugins/lua/OSkinConfigModel.lua."
    }

    on_run(function ()
        os.execv(os.programfile(), {"lua", path.join(os.projectdir(), "Data", "SKSE", "Plugins", "lua", "OSkinConfigModel.lua")})
    end)
task_end()
