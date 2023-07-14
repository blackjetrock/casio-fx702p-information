sudo kill `pgrep openocd`
sudo openocd -f interface/picoprobe.cfg -f target/rp2040.cfg -c "program ./fx702p_ram_replacement.elf verify reset exit"
