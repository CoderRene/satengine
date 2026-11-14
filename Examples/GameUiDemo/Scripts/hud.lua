local ui = sat.ui

function onStart()
    assert(ui.createModel("hud", {
        clicks = 0,
        active = false,
        pilot = "",
        greeting = "Hello, pilot Rene."
    }))
    local document = assert(ui.getDocument(self.entity))
    local increment = assert(ui.element(document, "increment"))
    local reset = assert(ui.element(document, "reset"))
    local name = assert(ui.element(document, "name"))

    local function setCount(value)
        ui.setModelValue("hud", "clicks", value)
        ui.setModelValue("hud", "active", value > 0)
    end

    assert(ui.on(increment, "click", function()
        setCount(ui.getModelValue("hud", "clicks") + 1)
    end))
    assert(ui.on(reset, "click", function()
        setCount(0)
    end))
    assert(ui.on(name, "change", function(event)
        local pilot = event.value ~= "" and event.value or "pilot"
        ui.setModelValue("hud", "greeting", "Hello, " .. pilot .. ".")
    end))
end
