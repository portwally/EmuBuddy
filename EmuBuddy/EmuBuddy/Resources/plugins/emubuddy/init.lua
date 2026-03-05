-- EmuBuddy Remote Control Plugin
-- Reads Lua commands from a file and executes them each frame.
-- EmuBuddy writes commands to the file; this plugin reads, executes, and deletes.

local exports = {}

exports.name = "emubuddy"
exports.version = "1.0.0"
exports.description = "EmuBuddy remote control via command file"
exports.license = "MIT"
exports.author = { name = "EmuBuddy" }

function exports.startplugin()
    -- Command file path: set via EMUBUDDY_CMD_FILE env var
    local cmd_file = os.getenv("EMUBUDDY_CMD_FILE")
    if not cmd_file then
        print("[emubuddy plugin] EMUBUDDY_CMD_FILE not set, plugin disabled")
        return
    end

    print("[emubuddy plugin] Watching command file: " .. cmd_file)

    -- Check for commands periodically (called ~60 times/sec)
    emu.register_periodic(function()
        local f = io.open(cmd_file, "r")
        if f then
            local cmd = f:read("*all")
            f:close()
            os.remove(cmd_file)
            if cmd and cmd ~= "" then
                -- Execute each line as a separate Lua command
                for line in cmd:gmatch("[^\r\n]+") do
                    local fn, err = load(line)
                    if fn then
                        local ok, result = pcall(fn)
                        if ok then
                            print("[emubuddy plugin] OK: " .. line)
                        else
                            print("[emubuddy plugin] Error executing: " .. line .. " -> " .. tostring(result))
                        end
                    else
                        print("[emubuddy plugin] Parse error: " .. line .. " -> " .. tostring(err))
                    end
                end
            end
        end
    end)
end

return exports
