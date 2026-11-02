class AndroidPortCommandFixture extends UTDrawCommandPanel;

var name FilledCategory;
var bool NeedsPlayer, LastItem;
var int Commands;

function FillEmoteList(name Category)
{
    FilledCategory = Category;
}

function bool NeedsPRIList()
{
    return NeedsPlayer;
}

function bool EndOfChain()
{
    return LastItem;
}

function bool PerformCommand()
{
    ++Commands;
    return false;
}
