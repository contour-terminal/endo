-- Endo DAP configuration for Neovim (nvim-dap)
--
-- Usage: copy this file to ~/.config/nvim/after/plugin/dap-endo.lua
-- or source it from your init.lua:
--   dofile(vim.fn.expand("path/to/dap.lua"))
--
-- Requirements: nvim-dap (https://github.com/mfussenegger/nvim-dap)
-- See docs/debugging/neovim.md for the full guide.

local dap = require("dap")

-- Adapter: tell nvim-dap how to launch the Endo debug adapter
dap.adapters.endo = {
  type = "executable",
  command = "endo",
  args = { "--dap" },
}

-- Launch configurations
dap.configurations.endo = {
  {
    type = "endo",
    request = "launch",
    name = "Launch Current File",
    program = "${file}",
    stopOnEntry = false,
  },
  {
    type = "endo",
    request = "launch",
    name = "Launch with Arguments",
    program = "${file}",
    args = function()
      local input = vim.fn.input("Script arguments: ")
      return vim.split(input, " ", { trimempty = true })
    end,
    stopOnEntry = false,
  },
  {
    type = "endo",
    request = "launch",
    name = "Launch (Stop on Entry)",
    program = "${file}",
    stopOnEntry = true,
  },
}

-- Filetype detection
vim.filetype.add({ extension = { endo = "endo" } })

-- Key bindings
vim.keymap.set("n", "<F5>", dap.continue, { desc = "Debug: Continue" })
vim.keymap.set("n", "<F10>", dap.step_over, { desc = "Debug: Step Over" })
vim.keymap.set("n", "<F11>", dap.step_into, { desc = "Debug: Step Into" })
vim.keymap.set("n", "<F12>", dap.step_out, { desc = "Debug: Step Out" })
vim.keymap.set("n", "<leader>b", dap.toggle_breakpoint, { desc = "Debug: Toggle Breakpoint" })
vim.keymap.set("n", "<leader>B", function()
  dap.set_breakpoint(vim.fn.input("Condition: "))
end, { desc = "Debug: Conditional Breakpoint" })
vim.keymap.set("n", "<leader>dr", dap.repl.open, { desc = "Debug: Open REPL" })
vim.keymap.set("n", "<leader>dl", dap.run_last, { desc = "Debug: Run Last" })
