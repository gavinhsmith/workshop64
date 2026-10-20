-- The prototype's obstacle course. y is a part's centre height above the floor.
return {
    camera = {9, 6, 9}, -- the fixed camera: high in a corner, looking over the course

    {name = "wall", pos = {-4, 1, 0}, size = {1, 2, 6}, color = {110, 120, 140}},
    {name = "steps",
        {pos = {0, 0.3, -4}, size = {2, 0.6, 2}, color = {196, 40, 28}},
        {pos = {0, 0.6, -6}, size = {2, 1.2, 2}, color = {245, 205, 48}},
        {pos = {0, 0.9, -8}, size = {2, 1.8, 2}, color = {75, 151, 75}},
    },
    {name = "ledge", pos = {3.5, 1.65, -8}, size = {3, 0.3, 2}, color = {13, 105, 172}},
    {name = "pillar", pos = {6, 1.5, -4}, size = {1, 3, 1}, color = {107, 50, 124}},
    {name = "curb", pos = {0, 0.15, 4}, size = {4, 0.3, 1}, color = {160, 165, 170}},
    {name = "statue", pos = {-7, 1.2, -7}, size = {1.6, 2.4, 0.8}, color = {230, 190, 60}, model = "statue"},
    {name = "crates", pos = {3.1, 0, -1},
        {pos = {-0.6, 0.5, 0}, size = {1, 1, 1}, color = {176, 120, 64}, model = "crate"},
        {pos = {0.6, 0.5, 0}, size = {1, 1, 1}, color = {176, 120, 64}, model = "crate"},
        {pos = {0, 1.5, 0}, size = {1, 1, 1}, color = {176, 120, 64}, model = "crate"},
    },
}
