
function main()
    log.info("Starting " .. astra.fullname)
    log.set({ color = true })

    if not dvbls then
        log.error("dvbls module is not found")
        astra.exit(1)
    end

    local dvb_list = dvbls()

    for _,dvb_info in pairs(dvb_list) do
        if dvb_info.error then
            log.error("adapter = " .. dvb_info.adapter .. ", device = " .. dvb_info.device)
            log.error("    check_device_fe(): " .. dvb_info.error)
        else
            if dvb_info.busy == true then
                log.warning("adapter = " .. dvb_info.adapter .. ", device = " .. dvb_info.device)
                log.warning("    adapter in use")
                log.warning("    mac = " .. dvb_info.mac)
                log.warning("    frontend = " .. dvb_info.frontend)
                log.warning("    type = " .. dvb_info.type)
            else
                log.info("adapter = " .. dvb_info.adapter .. ", device = " .. dvb_info.device)
                log.info("    mac = " .. dvb_info.mac)
                log.info("    frontend = " .. dvb_info.frontend)
                log.info("    type = " .. dvb_info.type)
            end
            if dvb_info.net_error then
                log.error("    check_device_net(): " .. dvb_info.net_error)
            end
        end
    end

    astra.exit()
end
