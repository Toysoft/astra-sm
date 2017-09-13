-- Astra Lua Library (Streaming)
-- https://cesbo.com/astra/
--
-- Copyright (C) 2013-2015, Andrey Dyldin <and@cesbo.com>
--               2015-2016, Artem Kharitonov <artem@3phase.pw>
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.

--      o      oooo   oooo     o      ooooo    ooooo  oooo ooooooooooo ooooooooooo
--     888      8888o  88     888      888       888  88   88    888    888    88
--    8  88     88 888o88    8  88     888         888         888      888ooo8
--   8oooo88    88   8888   8oooo88    888      o  888       888    oo  888    oo
-- o88o  o888o o88o    88 o88o  o888o o888ooooo88 o888o    o888oooo888 o888ooo8888


function on_analyze_spts(channel_data, input_id, data)
    local input_data = channel_data.input[input_id]
    local force_send = 0

    if data.error then
        log.error("[" .. input_data.config.name .. "] Error: " .. data.error)

    elseif data.psi then
        if dump_psi_info[data.psi] then
            dump_psi_info[data.psi]("[" .. input_data.config.name .. "] ", data)
        else
            log.error("[" .. input_data.config.name .. "] Unknown PSI: " .. data.psi)
        end

    elseif data.analyze then

        if data.on_air ~= input_data.on_air then
            local analyze_message = "[" .. input_data.config.name .. "] Bitrate:" .. data.total.bitrate .. "Kbit/s"

            if data.on_air == false then
                local m = nil
                if data.total.scrambled then
                    m = " Scrambled"
                else
                    m = " PES:" .. data.total.pes_errors .. " CC:" .. data.total.cc_errors
                end
                log.error(analyze_message .. m)
            else
                log.info(analyze_message)
            end

            input_data.on_air = data.on_air

            if channel_data.delay > 0 then
                if input_data.on_air == true and channel_data.active_input_id == 0 then
                    start_reserve(channel_data)
                else
                    channel_data.delay = channel_data.delay - 1
                    input_data.on_air = nil
                end
            else
                start_reserve(channel_data)
            end
        end

       if channel_data.request_timer == nil then
           channel_data.request_timer = 0
           force_send = 1
       end

       if channel_data.status == nil then
           channel_data.status = {
               type = 'channel',
               channel = channel_data.config.name,
           }
       end

       if channel_data.status.ready ~= data.on_air or channel_data.status.scrambled ~= data.total.scrambled then
           force_send = 1
       end

       channel_data.status.pnr = input_data.config.pnr
       channel_data.status.ready = data.on_air
       channel_data.status.scrambled = data.total.scrambled
       channel_data.status.bitrate = data.total.bitrate
       channel_data.status.cc_error = data.total.cc_errors
       channel_data.status.pes_error = data.total.pes_errors

       if channel_data.active_input_id == 0 then
           curr_input_id = 1
       else
           curr_input_id = channel_data.active_input_id
       end

       if channel_data.input[curr_input_id].config.format == 'dvb' then
           channel_data.status.stream = channel_data.input[curr_input_id].input.input.__options.name
       end

       if event_request and (channel_data.request_timer > event_request.interval or force_send == 1) then
           force_send = 0
           for key,value in pairs(channel_data.config.output) do
               channel_data.status.output = value:gsub("#.*","")
               channel_data.request_timer = 0
               send_json(channel_data.status)
           end
       end

       channel_data.request_timer = channel_data.request_timer + 1

    end
end

-- oooooooooo  ooooooooooo  oooooooo8 ooooooooooo oooooooooo ooooo  oooo ooooooooooo
--  888    888  888    88  888         888    88   888    888 888    88   888    88
--  888oooo88   888ooo8     888oooooo  888ooo8     888oooo88   888  88    888ooo8
--  888  88o    888    oo          888 888    oo   888  88o     88888     888    oo
-- o888o  88o8 o888ooo8888 o88oooo888 o888ooo8888 o888o  88o8    888     o888ooo8888

function start_reserve(channel_data)
    local active_input_id = 0
    for input_id, input_data in ipairs(channel_data.input) do
        if input_data.on_air == true then
            channel_data.transmit:set_upstream(input_data.input.tail:stream())
            log.info("[" .. channel_data.config.name .. "] Active input #" .. input_id)
            active_input_id = input_id
            break
        end
    end

    if active_input_id == 0 then
        local next_input_id = 0
        for input_id, input_data in ipairs(channel_data.input) do
            if not input_data.input then
                next_input_id = input_id
                break
            end
        end
        if next_input_id == 0 then
            log.error("[" .. channel_data.config.name .. "] Failed to switch to reserve")
        else
            channel_init_input(channel_data, next_input_id)
        end
    else
        channel_data.active_input_id = active_input_id
        channel_data.delay = channel_data.config.timeout

        for input_id, input_data in ipairs(channel_data.input) do
            if input_data.input and input_id > active_input_id then
                channel_kill_input(channel_data, input_id)
                log.debug("[" .. channel_data.config.name .. "] Destroy input #" .. input_id)
                input_data.on_air = nil
            end
        end
        collectgarbage()
    end
end

-- ooooo oooo   oooo oooooooooo ooooo  oooo ooooooooooo
--  888   8888o  88   888    888 888    88  88  888  88
--  888   88 888o88   888oooo88  888    88      888
--  888   88   8888   888        888    88      888
-- o888o o88o    88  o888o        888oo88      o888o

function channel_init_input(channel_data, input_id)
    local input_data = channel_data.input[input_id]
    -- merge channel-wide and input-specific PID maps
    if channel_data.config.map or input_data.config.map then
        local merged_map = {}
        if channel_data.config.map then
            merged_map = channel_data.config.map
        end
        if input_data.config.map then
            for _, input_val in pairs(input_data.config.map) do
                local found = false
                for chan_key, chan_val in pairs(merged_map) do
                    if input_val[1] == chan_val[1] then
                        -- override channel-wide mapping
                        merged_map[chan_key] = input_val
                        found = true
                    end
                end
                if found == false then
                    table.insert(merged_map, input_val)
                end
            end
        end
        input_data.config.map = merged_map
    end

    if channel_data.config.set_pnr then
        input_data.config.set_pnr = channel_data.config.set_pnr
    end

    input_data.input = init_input(input_data.config)

    if input_data.config.no_analyze ~= true then
       --table.dump(input_data)
        input_data.analyze = analyze({
            upstream = input_data.input.tail:stream(),
            name = input_data.config.name,
            cc_limit = input_data.config.cc_limit,
            bitrate_limit = input_data.config.bitrate_limit,
            callback = function(data)
                on_analyze_spts(channel_data, input_id, data)
            end,
        })
    end

    -- TODO: init additional modules

    channel_data.transmit:set_upstream(input_data.input.tail:stream())
end

function channel_kill_input(channel_data, input_id)
    local input_data = channel_data.input[input_id]

    -- TODO: kill additional modules

    input_data.analyze = nil
    input_data.on_air = nil

    kill_input(input_data.input)
    input_data.input = nil

    if input_id == 1 then channel_data.delay = 3 end
    channel_data.input[input_id] = { config = input_data.config, }
end

--   ooooooo  ooooo  oooo ooooooooooo oooooooooo ooooo  oooo ooooooooooo
-- o888   888o 888    88  88  888  88  888    888 888    88  88  888  88
-- 888     888 888    88      888      888oooo88  888    88      888
-- 888o   o888 888    88      888      888        888    88      888
--   88ooo88    888oo88      o888o    o888o        888oo88      o888o

init_output_option = {}
kill_output_option = {}

init_output_option.biss = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    if biss_encrypt == nil then
        log.error("[" .. output_data.config.name .. "] biss_encrypt module is not found")
        return nil
    end

    output_data.biss = biss_encrypt({
        upstream = channel_data.tail:stream(),
        key = output_data.config.biss,
    })

    channel_data.tail = output_data.biss
end

kill_output_option.biss = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.biss = nil
end

init_output_option.cbr = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    output_data.cbr = ts_cbr({
        upstream = channel_data.tail:stream(),
        name = output_data.config.name,
        rate = output_data.config.cbr,
        pcr_interval = output_data.config.cbr_pcr_interval,
        pcr_delay = output_data.config.cbr_pcr_delay,
        buffer_size = output_data.config.cbr_buffer_size,
    })

    channel_data.tail = output_data.cbr
end

kill_output_option.cbr = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.cbr = nil
end

-- TODO: remove this eventually
init_output_option.remux = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    error("[" .. output_data.config.name .. "] " ..
          "remux is no longer available, please use 'cbr=<BPS>' instead")
end

kill_output_option.remux = function(channel_data, output_id)
    --
end

init_output_module = {}
kill_output_module = {}

function channel_init_output(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    for key,_ in pairs(output_data.config) do
        if init_output_option[key] then
            init_output_option[key](channel_data, output_id)
        end
    end

    init_output_module[output_data.config.format](channel_data, output_id)
end

function channel_kill_output(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    for key,_ in pairs(output_data.config) do
        if kill_output_option[key] then
            kill_output_option[key](channel_data, output_id)
        end
    end

    kill_output_module[output_data.config.format](channel_data, output_id)
    channel_data.output[output_id] = { config = output_data.config, }
end

--   ooooooo            ooooo  oooo ooooooooo  oooooooooo
-- o888   888o           888    88   888    88o 888    888
-- 888     888 ooooooooo 888    88   888    888 888oooo88
-- 888o   o888           888    88   888    888 888
--   88ooo88              888oo88   o888ooo88  o888o

init_output_module.udp = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    local localaddr = output_data.config.localaddr
    if localaddr and ifaddr_list then
        local ifaddr = ifaddr_list[localaddr]
        if ifaddr and ifaddr.ipv4 then localaddr = ifaddr.ipv4[1] end
    end
    output_data.output = udp_output({
        upstream = channel_data.tail:stream(),
        addr = output_data.config.addr,
        port = output_data.config.port,
        ttl = output_data.config.ttl,
        localaddr = localaddr,
        socket_size = output_data.config.socket_size,
        rtp = (output_data.config.format == "rtp"),
        sync = output_data.config.sync,
        sync_opts = output_data.config.sync_opts,
    })
end

kill_output_module.udp = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = nil
end

init_output_module.rtp = function(channel_data, output_id)
    init_output_module.udp(channel_data, output_id)
end

kill_output_module.rtp = function(channel_data, output_id)
    kill_output_module.udp(channel_data, output_id)
end

--   ooooooo            ooooooooooo ooooo ooooo       ooooooooooo
-- o888   888o           888    88   888   888         888    88
-- 888     888 ooooooooo 888oo8      888   888         888ooo8
-- 888o   o888           888         888   888      o  888    oo
--   88ooo88            o888o       o888o o888ooooo88 o888ooo8888

init_output_module.file = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = file_output({
        upstream = channel_data.tail:stream(),
        filename = output_data.config.filename,
        m2ts = output_data.config.m2ts,
        buffer_size = output_data.config.buffer_size,
        aio = output_data.config.aio,
        directio = output_data.config.directio,
    })
end

kill_output_module.file = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = nil
end

--   ooooooo            ooooo ooooo ooooooooooo ooooooooooo oooooooooo
-- o888   888o           888   888  88  888  88 88  888  88  888    888
-- 888     888 ooooooooo 888ooo888      888         888      888oooo88
-- 888o   o888           888   888      888         888      888
--   88ooo88            o888o o888o    o888o       o888o    o888o

http_output_client_list = {}
http_output_instance_list = {}

function http_output_client(server, client, request)
    local client_data = server:data(client)

    if not request then
        http_output_client_list[client_data.client_id] = nil
        client_data.client_id = nil
        return nil
    end

    local function get_unique_client_id()
        local _id = math.random(10000000, 99000000)
        if http_output_client_list[_id] ~= nil then
            return nil
        end
        return _id
    end

    repeat
        client_data.client_id = get_unique_client_id()
    until client_data.client_id ~= nil

    http_output_client_list[client_data.client_id] = {
        server = server,
        client = client,
        request = request,
        st   = os.time(),
    }
end

function http_output_on_request(server, client, request)
    local client_data = server:data(client)

    if not request then
        if client_data.client_id then
            local channel_data = client_data.output_data.channel_data
            channel_data.clients = channel_data.clients - 1
            if channel_data.clients == 0 and channel_data.input[1].input ~= nil then
                for input_id, input_data in ipairs(channel_data.input) do
                    if input_data.input then
                        channel_kill_input(channel_data, input_id)
                    end
                end
                channel_data.active_input_id = 0
            end

            http_output_client(server, client, nil)
            collectgarbage()
        end
        return nil
    end

    client_data.output_data = server.__options.channel_list[request.path]
    if not client_data.output_data then
        server:abort(client, 404)
        return nil
    end

    http_output_client(server, client, request)

    local channel_data = client_data.output_data.channel_data
    channel_data.clients = channel_data.clients + 1

    local allow_channel = function()
        if not channel_data.input[1].input then
            channel_init_input(channel_data, 1)
        end

        server:send(client, {
            upstream = channel_data.tail:stream(),
            buffer_size = client_data.output_data.config.buffer_size,
            buffer_fill = client_data.output_data.config.buffer_fill,
        })
    end

    allow_channel()
end

init_output_module.http = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    local instance_id = output_data.config.host .. ":" .. output_data.config.port
    local instance = http_output_instance_list[instance_id]

    if not instance then
        instance = http_server({
            addr = output_data.config.host,
            port = output_data.config.port,
            sctp = output_data.config.sctp,
            route = {
                { "/*", http_upstream({ callback = http_output_on_request }) },
            },
            channel_list = {},
        })
        http_output_instance_list[instance_id] = instance
    end

    output_data.instance = instance
    output_data.instance_id = instance_id
    output_data.channel_data = channel_data

    instance.__options.channel_list[output_data.config.path] = output_data
end

kill_output_module.http = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    local instance = output_data.instance
    local instance_id = output_data.instance_id

    for _, client in pairs(http_output_client_list) do
        if client.server == instance then
            instance:close(client.client)
        end
    end

    instance.__options.channel_list[output_data.config.path] = nil

    local is_instance_empty = true
    for _ in pairs(instance.__options.channel_list) do
        is_instance_empty = false
        break
    end

    if is_instance_empty then
        instance:close()
        http_output_instance_list[instance_id] = nil
    end

    output_data.instance = nil
    output_data.instance_id = nil
    output_data.channel_data = nil
end

--   ooooooo            oooo   oooo oooooooooo
-- o888   888o           8888o  88   888    888
-- 888     888 ooooooooo 88 888o88   888oooo88
-- 888o   o888           88   8888   888
--   88ooo88            o88o    88  o888o

init_output_module.np = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    local conf = output_data.config

    local http_conf = {
        host = conf.host,
        port = conf.port,
        path = conf.path,
        upstream = channel_data.tail:stream(),
        buffer_size = conf.buffer_size,
        buffer_fill = conf.buffer_size,
        timeout = conf.timeout,
        sctp = conf.sctp,
        headers = {
            "User-Agent: " .. http_user_agent,
            "Host: " .. conf.host,
            "Connection: keep-alive",
        },
    }

    local timer_conf = {
        interval = 5,
        callback = function(self)
            output_data.timeout:close()
            output_data.timeout = nil

            if output_data.request then output_data.request:close() end
            output_data.request = http_request(http_conf)
        end
    }

    http_conf.callback = function(self, response)
        if not response then
            output_data.request:close()
            output_data.request = nil
            output_data.timeout = timer(timer_conf)

        elseif response.code == 200 then
            if output_data.timeout then
                output_data.timeout:close()
                output_data.timeout = nil
            end

        elseif response.code == 301 or response.code == 302 then
            if output_data.timeout then
                output_data.timeout:close()
                output_data.timeout = nil
            end

            output_data.request:close()
            output_data.request = nil

            local o = parse_url(response.headers["location"])
            if o then
                http_conf.host = o.host
                http_conf.port = o.port
                http_conf.path = o.path
                http_conf.headers[2] = "Host: " .. o.host

                log.info("[" .. conf.name .. "] Redirect to http://" .. o.host .. ":" .. o.port .. o.path)
                output_data.request = http_request(http_conf)
            else
                log.error("[" .. conf.name .. "] NP Error: Redirect failed")
                output_data.timeout = timer(timer_conf)
            end

        else
            output_data.request:close()
            output_data.request = nil
            log.error("[" .. conf.name .. "] NP Error: " .. response.code .. ":" .. response.message)
            output_data.timeout = timer(timer_conf)
        end
    end

    output_data.request = http_request(http_conf)
end

kill_output_module.np = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    if output_data.timeout then
        output_data.timeout:close()
        output_data.timeout = nil
    end
    if output_data.request then
        output_data.request:close()
        output_data.request = nil
    end
end

--
-- Output: pipe://
--

init_output_module.pipe = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = pipe_generic({
        upstream = channel_data.tail:stream(),
        name = "pipe_output " .. channel_data.config.name .. " #" .. output_id,
        command = output_data.config.command,
        restart = output_data.config.restart,
    })
end

kill_output_module.pipe = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = nil
end

--
-- Output: it95x://
--

function make_it95x(conf)
    -- check parameters
    if _G["it95x_output"] == nil then
        error("[make_it95x] this feature is not available on this platform")
    end
    if conf == nil or conf.name == nil then
        error("[make_it95x] option 'name' is required")
    end

    -- instantiate output module
    local cbr_conf = conf.cbr
    conf.cbr = nil

    local mod_it95x = it95x_output(conf)
    local head = mod_it95x

    -- instantiate CBR muxer if needed
    local mod_cbr = nil
    if cbr_conf ~= false then
        if cbr_conf == nil then
            cbr_conf = {}
        elseif type(cbr_conf) == "number" then
            cbr_conf = {
                rate = cbr_conf,
            }
        elseif type(cbr_conf) ~= "table" then
            error("[make_it95x " .. conf.name .. "] option 'cbr' has wrong format")
        end

        if cbr_conf.rate == nil then
            local bitrate = mod_it95x:bitrate()
            if type(bitrate) == "number" then
                -- use channel rate as target bitrate
                cbr_conf.rate = bitrate
            else
                -- CBR is disabled by default for ISDB-T partial mode
                log.debug("[make_it95x " .. conf.name .. "] not using CBR for partial reception mode")
            end
        end

        if cbr_conf.rate ~= nil then
            local units = " bps"
            if cbr_conf.rate <= 1000 then
                units = " mbps"
            end
            log.debug("[make_it95x " .. conf.name .. "] padding output TS to " .. cbr_conf.rate .. units)

            cbr_conf.name = conf.name
            mod_cbr = ts_cbr(cbr_conf)
            mod_it95x:set_upstream(mod_cbr:stream())
            head = mod_cbr
        end
    end

    local instance = {
        name = conf.name,
        head = head,
        it95x = mod_it95x,
        cbr = mod_cbr,
        busy = false,
    }

    return instance
end

init_output_module.it95x = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    local name = output_data.config.name

    local instance = _G[output_data.config.addr]
    local function check_instance()
        if instance == nil then return false end
        if type(instance) ~= "table" then return false end
        if instance.name == nil then return false end
        if instance.it95x == nil then return false end
        if tostring(instance.it95x) ~= "it95x_output" then return false end
        if instance.head == nil then return false end
        return true
    end
    if not check_instance() then
        error("[" .. name .. "] it95x definition not found")
    end
    if instance.busy then
        error("[" .. name .. "] it95x output '" .. instance.name .. "' is already in use")
    end

    instance.busy = true
    instance.head:set_upstream(channel_data.tail:stream())
    output_data.output = instance
end

kill_output_module.it95x = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    local instance = output_data.output

    output_data.output = nil
    instance.busy = false
end

--
-- Transform
--

init_transform_module = {}
kill_transform_module = {}

function stream_init_transform(stream_data, xfrm_id)
    local xfrm_data = stream_data.transform[xfrm_id]

    local conf = {}
    for k, v in pairs(xfrm_data.config) do
        conf[k] = v
    end

    conf.format = nil
    conf.upstream = stream_data.tail:stream()

    local xfrm = init_transform_module[xfrm_data.config.format](conf)
    xfrm_data.transform = xfrm
    stream_data.tail = xfrm
end

function stream_kill_transform(stream_data, xfrm_id)
    local xfrm_data = stream_data.transform[xfrm_id]

    kill_transform_module[xfrm_data.config.format](xfrm_data)
    stream_data.transform[xfrm_id] = { config = xfrm_data.config }
end

--
-- Transform: cbr
--

init_transform_module.cbr = function(conf)
    return ts_cbr(conf)
end

kill_transform_module.cbr = function(instance)
    --
end

--
-- Transform: pipe
--

init_transform_module.pipe = function(conf)
    conf.name = "pipe_xfrm " .. conf.name
    conf.stream = true

    return pipe_generic(conf)
end

kill_transform_module.pipe = function(instance)
    --
end

--   oooooooo8 ooooo ooooo      o      oooo   oooo oooo   oooo ooooooooooo ooooo
-- o888     88  888   888      888      8888o  88   8888o  88   888    88   888
-- 888          888ooo888     8  88     88 888o88   88 888o88   888ooo8     888
-- 888o     oo  888   888    8oooo88    88   8888   88   8888   888    oo   888      o
--  888oooo88  o888o o888o o88o  o888o o88o    88  o88o    88  o888ooo8888 o888ooooo88

channel_list = {}

function make_channel(channel_config)
    if not channel_config.name then
        log.error("[make_channel] option 'name' is required")
        return nil
    end

    if not channel_config.input or #channel_config.input == 0 then
        log.error("[" .. channel_config.name .. "] option 'input' is required")
        return nil
    end

    if channel_config.transform == nil then channel_config.transform = {} end
    if channel_config.output == nil then channel_config.output = {} end
    if channel_config.timeout == nil then channel_config.timeout = 0 end
    if channel_config.enable == nil then channel_config.enable = true end

    if channel_config.enable == false then
        log.info("[" .. channel_config.name .. "] channel is disabled")
        return nil
    end

    local channel_data = {
        config = channel_config,
        input = {},
        transform = {},
        output = {},
        delay = 3,
        clients = 0,
    }

    local function check_url_format(obj)
        local url_list = channel_config[obj]
        local config_list = channel_data[obj]
        local module_list = _G["init_" .. obj .. "_module"]
        local function check_module(config)
            if not config then return false end
            if not config.format then return false end
            if not module_list[config.format] then return false end
            return true
        end
        for n, url in ipairs(url_list) do
            local item = {}
            if type(url) == "string" then
                item.config = parse_url(url)
            elseif type(url) == "table" then
                if url.url then
                    local u = parse_url(url.url)
                    for k,v in pairs(u) do url[k] = v end
                end
                item.config = url
            end
            if not check_module(item.config) then
                log.error("[" .. channel_config.name .. "] wrong " .. obj .. " #" .. n .. " format")
                return false
            end
            item.config.name = channel_config.name .. " #" .. n
            table.insert(config_list, item)
        end
        return true
    end

    if not check_url_format("input") then return nil end
    if not check_url_format("transform") then return nil end
    if not check_url_format("output") then return nil end

    if #channel_data.output == 0 then
        channel_data.clients = 1
    else
        for _, o in pairs(channel_data.output) do
            if o.config.format ~= "http" or o.config.keep_active == true then
                channel_data.clients = channel_data.clients + 1
            end
        end
    end

    channel_data.active_input_id = 0
    channel_data.transmit = transmit()
    channel_data.tail = channel_data.transmit

    for xfrm_id in ipairs(channel_data.transform) do
        stream_init_transform(channel_data, xfrm_id)
    end

    if channel_data.clients > 0 then
        channel_init_input(channel_data, 1)
    end

    for output_id in ipairs(channel_data.output) do
        channel_init_output(channel_data, output_id)
    end

    table.insert(channel_list, channel_data)
    return channel_data
end

function kill_channel(channel_data)
    if not channel_data then return nil end

    local channel_id = 0
    for key, value in pairs(channel_list) do
        if value == channel_data then
            channel_id = key
            break
        end
    end

    if channel_id == 0 then
        log.error("[kill_channel] channel is not found")
        return nil
    end

    while #channel_data.input > 0 do
        channel_kill_input(channel_data, 1)
        table.remove(channel_data.input, 1)
    end
    channel_data.input = nil

    while #channel_data.transform > 0 do
        stream_kill_transform(channel_data, 1)
        table.remove(channel_data.transform, 1)
    end
    channel_data.transform = nil

    while #channel_data.output > 0 do
        channel_kill_output(channel_data, 1)
        table.remove(channel_data.output, 1)
    end
    channel_data.output = nil

    channel_data.tail = nil
    channel_data.transmit = nil
    channel_data.config = nil

    table.remove(channel_list, channel_id)
    collectgarbage()
end

function find_channel(key, value)
    for _, channel_data in pairs(channel_list) do
        if channel_data.config[key] == value then
            return channel_data
        end
    end
    return nil
end
