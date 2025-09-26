vim.opt.tabstop = 8
vim.opt.shiftwidth = 8

vim.api.nvim_create_autocmd({ "BufReadPost", "BufNewFile" }, {
	pattern = "*.asm",
	command = "setl filetype=nasm",
})

return {}
