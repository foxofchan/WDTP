/*
  ==============================================================================

    FileTreeContainer.cpp
    Created: 7 Sep 2016 7:37:28am
    Author:  SwingCoder

  ==============================================================================
*/

#include "WdtpHeader.h"

extern PropertiesFile* systemFile;

File FileTreeContainer::projectFile;
ValueTree FileTreeContainer::projectTree = ValueTree::invalid;

//==============================================================================
FileTreeContainer::FileTreeContainer (EditAndPreview* rightArea) :
    editAndPreview (rightArea)
{
    jassert (editAndPreview != nullptr);
    projectFile = File::nonexistent;

    // fileTree...
    fileTree.setRootItem (nullptr);
    fileTree.setRootItemVisible (true);
    fileTree.setDefaultOpenness (true);
    fileTree.setMultiSelectEnabled (true);
    fileTree.setOpenCloseButtonsVisible (true);
    fileTree.setIndentSize (15);
    fileTree.getViewport()->setScrollBarThickness (10);
    fileTree.setColour (TreeView::selectedItemBackgroundColourId, Colours::skyblue.withAlpha (0.6f));

    addAndMakeVisible (fileTree);
}

//=========================================================================
FileTreeContainer::~FileTreeContainer()
{
    fileTree.setRootItem (nullptr);

    projectTree = ValueTree::invalid;
    projectFile = File::nonexistent;
}

//=========================================================================
void FileTreeContainer::resized()
{
    fileTree.setVisible (getWidth() > 50);
    fileTree.setBounds (12, 3, getWidth() - 14, getHeight() - 6);
}

//=================================================================================================
void FileTreeContainer::paint (Graphics& g)
{
    g.setColour (Colours::grey);
    g.drawVerticalLine (getWidth() - 1, 0, getBottom() - 0.f);
}

//=================================================================================================
void FileTreeContainer::openProject (const File& project)
{
    // check if the file exists and could write into
    if (!(project.existsAsFile() && project.hasWriteAccess()))
    {
        AlertWindow::showMessageBox (AlertWindow::InfoIcon, TRANS ("Message"),
                                     TRANS ("Project file \"") + project.getFullPathName() +
                                     "\" " + TRANS ("is nonexistent or cannot be written to."));
        return;
    }

    File realProject = project;

    // check if this is a normal project or a packed project
    if (project.getFileExtension() == ".wpck")
    {

        ZipFile zip (project);
        const bool notZip = zip.getNumEntries() < 1;
        const File unpackDir (project.getSiblingFile (project.getFileNameWithoutExtension()));
        String message (zip.uncompressTo (unpackDir).getErrorMessage());

        if (notZip)
            message = TRANS ("Invalid packed project.");

        if (message.isNotEmpty() || notZip)
            SHOW_MESSAGE (TRANS ("Unpack failed:") + newLine + message);
        else   // the project file after unpacked
            realProject = unpackDir.getChildFile (project.getFileNameWithoutExtension() + ".wdtp");
    }

    // start a new instance of this app
    if (projectTree.isValid())
    {
        Process::openDocument (File::getSpecialLocation (File::currentApplicationFile).getFullPathName(),
                               realProject.getFullPathName());

        return;
    }

    // for backward compatibility (0.9.170217)
    // check the file has been gziped or not
    MemoryBlock mb;
    realProject.loadFileAsData (mb);
    const uint8* const data = (const uint8*)mb.getData();
    const bool isGzip = ((int)data[0] == 120 && (int)data[1] == 218);
    // should remove above at some point (backward compatibility)

    projectTree = SwingUtilities::readValueTreeFromFile (realProject, isGzip);

    // check if this is an vaild project file
    if (projectTree.getType().toString() != "wdtpProject")
    {
        AlertWindow::showMessageBox (AlertWindow::InfoIcon, TRANS ("Message"),
                                     TRANS ("An invalid project file."));
        return;
    }

    // load the project
    projectFile = realProject;
    sorter = new ItemSorter (projectTree);
    docTreeItem = new DocTreeViewItem (projectTree, this, sorter);
    sorter->setTreeViewItem (docTreeItem);

    fileTree.setRootItem (docTreeItem);
    
    // select the last item
    const String& lastItem = projectTree.getProperty ("identityOfLastSelectedItem").toString();
    TreeViewItem* item = fileTree.findItemFromIdentifierString (lastItem);

    if (item == nullptr)
        item = fileTree.getRootItem();

    item->setSelected (true, true);

    // change the text of main window's title-bar
    MainWindow* mainWindow = dynamic_cast<MainWindow*>(getTopLevelComponent());
    jassert (mainWindow != nullptr);

    mainWindow->setName (JUCEApplication::getInstance()->getApplicationName() + " - " +
                         realProject.getFileNameWithoutExtension());

    // set the main-window's size and position
    const String& sizeAndPostion (projectTree.getProperty ("mainWindowSizeAndPosition").toString());

    if (sizeAndPostion.isNotEmpty())
        mainWindow->restoreWindowStateFromString (sizeAndPostion);

    // add the project to recent opened file list
    RecentlyOpenedFilesList  recentFiles;
    recentFiles.setMaxNumberOfItems (10);
    recentFiles.removeNonExistentFiles();
    recentFiles.restoreFromString (systemFile->getValue ("recentFiles"));
    recentFiles.addFile (realProject);

    systemFile->setValue ("recentFiles", recentFiles.toString());
    fileTree.scrollToKeepItemVisible (item);
}

//=================================================================================================
void FileTreeContainer::closeProject()
{
    if (hasLoadedProject())
    {
        // store the main-window's size and position
        MainWindow* mainWindow = dynamic_cast<MainWindow*>(getTopLevelComponent());
        jassert (mainWindow != nullptr);

        const String& sizeAndPosition (mainWindow->getWindowStateAsString());
        projectTree.setProperty ("mainWindowSizeAndPosition", sizeAndPosition, nullptr);

        if (saveDocAndProject())
        {
            fileTree.setRootItem (nullptr);
            docTreeItem = nullptr;
            sorter = nullptr;
            projectTree = ValueTree::invalid;
            projectFile = File::nonexistent;
            editAndPreview->projectClosed();

            // change the text of main window's title-bar           
            mainWindow->setName (JUCEApplication::getInstance()->getApplicationName());
        }
    }
}

//=================================================================================================
const bool FileTreeContainer::saveDocAndProject()
{
    // Here must check to prevent invalid assert 
    // eg. when quit this application after closed project..
    if (projectTree.isValid())
        return editAndPreview->saveCurrentDocIfChanged() && saveProject();

    return true;
}

//=================================================================================================
ItemSorter::ItemSorter (ValueTree& tree_)
    : projectTree (tree_)
{
    jassert (projectTree.isValid());

    order.setValue (projectTree.getProperty ("order"));
    showWhat.setValue (projectTree.getProperty ("showWhat"));
    tooltip.setValue (projectTree.getProperty ("tooltip"));
    ascending.setValue (projectTree.getProperty ("ascending"));
    dirFirst.setValue (projectTree.getProperty ("dirFirst"));

    order.addListener (this);
    showWhat.addListener (this);
    ascending.addListener (this);
    tooltip.addListener (this);
    dirFirst.addListener (this);
}

//=========================================================================
ItemSorter::~ItemSorter()
{
    order.removeListener (this);
    showWhat.removeListener (this);
    tooltip.removeListener (this);
    ascending.removeListener (this);
    dirFirst.removeListener (this);
}

//=================================================================================================
const int ItemSorter::compareElements (TreeViewItem* first, TreeViewItem* second) const
{
    DocTreeViewItem* f = dynamic_cast<DocTreeViewItem*> (first);
    DocTreeViewItem* s = dynamic_cast<DocTreeViewItem*> (second);

    if (f == nullptr || s == nullptr)
        return 0;

    const ValueTree& ft (f->getTree());
    const ValueTree& st (s->getTree());

    // root tree
    if (ft.getType().toString() == "wdtpProject")
        return -1;

    if (st.getType().toString() == "wdtpProject")
        return 1;

    const File& ff (DocTreeViewItem::getMdFileOrDir (ft));
    const File& sf (DocTreeViewItem::getMdFileOrDir (st));
    const bool isAscending = (ascending.getValue() == var (0));
    const bool isDirFirst = (dirFirst.getValue() == var (0));

    // one is dir or both are dir, or both are doc. here must use the item's ValueTree
    // rather than it's disk file because the file maybe nonexists (red item)..
    if (ft.getType().toString() == "dir" && st.getType().toString() == "doc")
    {
        return isDirFirst ? -1 : 1;
    }
    else if (ft.getType().toString() == "doc" && st.getType().toString() == "dir")
    {
        return isDirFirst ? 1 : -1;
    }
    else  // doc vs doc and dir vs dir..
    {
        if (0 == order) // file name
        {
            const int r = ft.getProperty ("name").toString().compareIgnoreCase (st.getProperty ("name").toString());
            return isAscending ? r : -r;
        }
        else if (1 == order) // title or descrition
        {
            const int r = ft.getProperty ("title").toString().compareIgnoreCase (st.getProperty ("title").toString());
            return isAscending ? r : -r;
        }
        else if (3 == order) // file size
        {
            const int r = int (ff.getSize() - sf.getSize());
            return isAscending ? r : -r;
        }
        else if (4 == order) // create time
        {
            if (!(ff.exists() && sf.exists()))
                return 0;

            const int r = ft.getProperty ("createDate").toString().compareIgnoreCase
            (st.getProperty ("createDate").toString());
            return isAscending ? -r : r;
        }
        else if (5 == order) // modified time
        {
            if (!(ff.exists() && sf.exists()))
                return 0;

            const int r = ft.getProperty ("modifyDate").toString().compareIgnoreCase
            (st.getProperty ("modifyDate").toString());
            return isAscending ? -r : r;
        }
    }

    jassertfalse;  // should never get here..
    return 0;
}

//=================================================================================================
void ItemSorter::valueChanged (Value& value)
{
    // haven't called setTreeViewItem() yet? See this class' description..
    jassert (rootItem != nullptr);

    ScopedPointer<XmlElement> treeViewState (rootItem->getOwnerView()->getOpennessState (true));
    rootItem->refreshDisplay();

    if (treeViewState != nullptr)
        rootItem->getOwnerView()->restoreOpennessState (*treeViewState, true);

    // update projectTree
    if (value.refersToSameSourceAs (order))
        projectTree.setProperty ("order", order.getValue(), nullptr);
    else if (value.refersToSameSourceAs (showWhat))
        projectTree.setProperty ("showWhat", showWhat.getValue(), nullptr);
    else if (value.refersToSameSourceAs (ascending))
        projectTree.setProperty ("ascending", ascending.getValue(), nullptr);
    else if (value.refersToSameSourceAs (tooltip))
        projectTree.setProperty ("tooltip", tooltip.getValue(), nullptr);
    else if (value.refersToSameSourceAs (dirFirst))
        projectTree.setProperty ("dirFirst", dirFirst.getValue(), nullptr);

    // save the project file
    FileTreeContainer::saveProject();
}

//=================================================================================================
void FileTreeContainer::setIdentityOfLastSelectedItem (const String& str)
{
    projectTree.setProperty ("identityOfLastSelectedItem", str, nullptr);
    saveProject();
}

//=================================================================================================
const bool FileTreeContainer::aDocSelectedCurrently() const
{
    DocTreeViewItem* item = static_cast<DocTreeViewItem*>(fileTree.getSelectedItem (0));

    if (item != nullptr)
        return item->getTree().getType().toString() == "doc";
    else
        return false;
}

//=================================================================================================
void FileTreeContainer::reloadCurrentDoc()
{
    DocTreeViewItem* item = static_cast<DocTreeViewItem*>(fileTree.getSelectedItem (0));

    if (item != nullptr && item->getTree().getType().toString() == "doc")
    {
        const ValueTree& currentTree (item->getTree());
        const String& fileContent (DocTreeViewItem::getMdFileOrDir (currentTree).loadFileAsString());

        if (editAndPreview->getCurrentContent().compareNatural (fileContent, true) != 0)
        {
            fileTree.getRootItem()->setSelected (true, true);
            item->needCreate (currentTree);
            item->setSelected (true, true);
        }
    }
}

//=================================================================================================
bool FileTreeContainer::saveProject()
{
    if (!SwingUtilities::writeValueTreeToFile (projectTree, projectFile, true))
    {
        SHOW_MESSAGE (TRANS ("Something wrong during saving this project."));
        return false;
    }

    return true;
}

//=================================================================================================
const bool FileTreeContainer::selectItemFromHtmlFile (const File& htmlFile)
{
    // get file's path relative to "../site"
    String htmlPath (htmlFile.getFullPathName()
                     .fromFirstOccurrenceOf (projectFile.getSiblingFile ("site").getFullPathName(), false, false));

    // remove ".html" and the first character '/'
    htmlPath = htmlPath.dropLastCharacters (5).substring (1);  

    // dir remove "/index"
    if (htmlPath.getLastCharacters (5) == "index")
        htmlPath = htmlPath.dropLastCharacters (6);

    if (htmlPath.isEmpty())  // root
    {
        fileTree.getItemOnRow (0)->setSelected (true, true);
        return true;
    }

    //DBGX(htmlPath);

    // find match item and select it
    // here must shrink it first and then open it to make sure it'll open totally
    fileTree.getRootItem()->setOpen (false);
    fileTree.getRootItem()->setOpen (true);

    for (int i = fileTree.getNumRowsInTree(); --i >= 1; )  // 0 is root, see above
    {
        DocTreeViewItem* item = dynamic_cast<DocTreeViewItem*>(fileTree.getItemOnRow (i));
        jassert (item != nullptr);
        ValueTree v = item->getTree();

        // get the tree's full path
        const ValueTree orignal (v.createCopy());
        String treePath = v.getProperty ("name").toString();
        v = v.getParent();

        while (v.isValid() && v.getType().toString() != "wdtpProject")
        {
            treePath = v.getProperty ("name").toString() + File::separatorString + treePath;
            v = v.getParent();
        }

        v = orignal.createCopy();
        //DBGX(treePath);

        if (htmlPath == treePath)
        {
            item->setSelected (true, true);
            fileTree.scrollToKeepItemVisible (item);

            return true;
        }
    }

    return false;
}

