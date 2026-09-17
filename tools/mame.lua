-- Test-harness autoboot script for headless MAME runs of Bomb Squad.
--
-- The plan comes in through the BS_PLAN environment variable as a comma
-- separated list of "<frame>:<action>" steps, executed in order:
--
--   1200:snap          take a screenshot
--   300:type:HELLO     post text through the natural keyboard
--   600:key:{ENTER}    post a coded key (MAME's {NAME} syntax)
--   1800:quit          stop the emulation
--   1500:reset         press RESET (RAM survives)
--   900:peek:1BF6:16   print 16 bytes of memory to stdout
--
-- MAME re-runs the autoboot script after a soft reset, and the plan must not
-- start over when it does: a plan with a reset step in it would otherwise
-- reset for ever. The globals live in MAME's own Lua state, so the second
-- run can see that the first one is still going.
if bs_running then return end
bs_running = true

local plan = {}
for step in (os.getenv("BS_PLAN") or ""):gmatch("[^,]+") do
    local frame, action = step:match("^(%d+):(.+)$")
    if frame then plan[#plan + 1] = { frame = tonumber(frame), action = action } end
end

local next_step = 1
local frame = 0
held = {}

-- The subscription has to be kept alive or MAME drops it at the next GC.
subscription = emu.add_machine_frame_notifier(function ()
    frame = frame + 1
    for i = #held, 1, -1 do
        if frame >= held[i].until_frame then
            held[i].field:set_value(0)
            table.remove(held, i)
        end
    end
    while next_step <= #plan and plan[next_step].frame <= frame do
        local action = plan[next_step].action
        next_step = next_step + 1
        if action == "snap" then
            manager.machine.video:snapshot()
        elseif action == "quit" then
            manager.machine:exit()
        elseif action == "reset" then
            -- The machine's RESET button, which restarts CAOS but leaves the
            -- RAM alone -- that is what a resident menu entry has to survive.
            -- MAME has no such button for the KC and its own soft_reset()
            -- zeroes the RAM, so send the CPU to the ROM's reset entry and
            -- let CAOS put the machine back in order itself.
            manager.machine.devices[":maincpu"].state["PC"].value = 0xE000
        elseif action:match("^pc:") then
            -- force a jump, to enter a routine the keyboard cannot reach
            local addr = tonumber(action:match("^pc:(%S+)$"), 16)
            manager.machine.devices[":maincpu"].state["PC"].value = addr
        elseif action == "regs" then
            local st = manager.machine.devices[":maincpu"].state
            print(string.format("REGS PC=%04X IX=%04X IY=%04X SP=%04X",
                                st["PC"].value, st["IX"].value, st["IY"].value,
                                st["SP"].value))
        elseif action:match("^peek:") then
            local addr, len = action:match("^peek:(%x+):(%d+)$")
            addr, len = tonumber(addr, 16), tonumber(len)
            local space = manager.machine.devices[":maincpu"].spaces["program"]
            local out = {}
            for i = 0, len - 1 do
                out[#out + 1] = string.format("%02X", space:read_u8(addr + i))
            end
            print(string.format("PEEK %04X %s", addr, table.concat(out, " ")))
        elseif action == "listports" then
            for tag, port in pairs(manager.machine.ioport.ports) do
                for name, field in pairs(port.fields) do
                    print(string.format("PORT %s | %s", tag, name))
                end
            end
        elseif action:match("^press:") then
            -- hold a raw key matrix field down for a few frames
            local tag, name, frames = action:match("^press:([^|]+)|([^|]+)|(%d+)$")
            local field = manager.machine.ioport.ports[tag].fields[name]
            field:set_value(1)
            held[#held + 1] = { field = field, until_frame = frame + tonumber(frames) }
        elseif action:match("^nz:") then
            -- report where a range stops being uniform, to find stray writes
            local from, to = action:match("^nz:(%x+):(%x+)$")
            from, to = tonumber(from, 16), tonumber(to, 16)
            local space = manager.machine.devices[":maincpu"].spaces["program"]
            local hits, n = {}, 0
            for addr = from, to do
                if space:read_u8(addr) ~= 0 then
                    n = n + 1
                    if #hits < 24 then hits[#hits + 1] = string.format("%04X", addr) end
                end
            end
            print(string.format("NZ %04X-%04X count=%d %s", from, to, n,
                                table.concat(hits, " ")))
        elseif action:match("^load:") then
            -- Poke a raw binary into RAM: load:FILE:ADDR (addr hex, 0x ok)
            local file, addr = action:match("^load:(.+):(%S+)$")
            addr = tonumber(addr, 16)
            local f = io.open(file, "rb")
            if not f then
                print(string.format("LOAD: cannot open %s", file))
            else
                local data = f:read("*a")
                f:close()
                local space = manager.machine.devices[":maincpu"].spaces["program"]
                for i = 1, #data do
                    space:write_u8(addr + i - 1, string.byte(data, i))
                end
                print(string.format("LOAD: %d bytes at $%04X from %s", #data, addr, file))
            end
        else
            local kind, arg = action:match("^(%a+):(.*)$")
            if kind == "type" then
                manager.machine.natkeyboard:post(arg)
            elseif kind == "key" then
                manager.machine.natkeyboard:post_coded(arg)
            end
        end
    end
end)
