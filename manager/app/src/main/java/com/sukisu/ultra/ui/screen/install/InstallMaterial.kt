package com.sukisu.ultra.ui.screen.install

import com.sukisu.ultra.BuildConfig

import android.app.Activity
import android.content.Intent
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.DriveFileMove
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.SdStorage
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import com.sukisu.ultra.R
import com.sukisu.ultra.getKernelVersion
import com.sukisu.ultra.ui.component.choosekmidialog.ChooseKmiDialog
import com.sukisu.ultra.ui.component.dialog.rememberConfirmDialog
import com.sukisu.ultra.ui.component.material.ExpressiveColumn
import com.sukisu.ultra.ui.component.material.ExpressiveDropdownItem
import com.sukisu.ultra.ui.component.material.ExpressiveListItem
import com.sukisu.ultra.ui.component.material.ExpressiveRadioItem
import com.sukisu.ultra.ui.kernelFlash.KpmPatchOption
import com.sukisu.ultra.ui.kernelFlash.KpmPatchSelectionDialogMaterial
import com.sukisu.ultra.ui.kernelFlash.component.SlotSelectionDialogMaterial
import com.sukisu.ultra.ui.kernelFlash.rememberAnyKernel3State
import com.sukisu.ultra.ui.navigation3.LocalNavigator
import com.sukisu.ultra.ui.navigation3.Route
import com.sukisu.ultra.ui.screen.flash.FlashIt
import com.sukisu.ultra.ui.util.LkmSelection
import com.sukisu.ultra.ui.util.getAvailablePartitions
import com.sukisu.ultra.ui.util.getCurrentKmi
import com.sukisu.ultra.ui.util.getDefaultPartition
import com.sukisu.ultra.ui.util.getSlotSuffix
import com.sukisu.ultra.ui.util.isAbDevice
import com.sukisu.ultra.ui.util.rootAvailable

/**
 * @author weishu
 * @date 2024/3/12.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InstallScreenMaterial(preselectedKernelUri: String? = null) {
    val navigator = LocalNavigator.current
    val context = LocalContext.current

    val installMethodState = remember {
        mutableStateOf<InstallMethod?>(null)
    }
    var installMethod by installMethodState

    var lkmSelection by remember {
        mutableStateOf<LkmSelection>(LkmSelection.KmiNone)
    }

    val kernelVersion = getKernelVersion()
    val isGKI = kernelVersion.isGKI()
    val isAbDevice = produceState(initialValue = false) {
        value = isAbDevice()
    }.value

    var partitionSelectionIndex by remember { mutableIntStateOf(0) }
    var partitionsState by remember { mutableStateOf<List<String>>(emptyList()) }
    var hasCustomSelected by remember { mutableStateOf(false) }

    val horizonKernelSummary = stringResource(R.string.horizon_kernel_summary)
    val anyKernel3State = rememberAnyKernel3State(
        installMethodState = installMethodState,
        preselectedKernelUri = preselectedKernelUri,
        horizonKernelSummary = horizonKernelSummary,
        isAbDevice = isAbDevice
    )

    val kpmPatchOption = anyKernel3State.kpmPatchOption

    val onInstall = {
        installMethod?.let { method ->
            when (method) {
                is InstallMethod.HorizonKernel -> {
                    method.uri?.let { uri ->
                        navigator.push(
                            Route.KernelFlash(
                                kernelUri = uri,
                                selectedSlot = method.slot,
                                kpmPatchEnabled = kpmPatchOption == KpmPatchOption.PATCH_KPM,
                                kpmUndoPatch = kpmPatchOption == KpmPatchOption.UNDO_PATCH_KPM
                            )
                        )
                    }
                }
                else -> {
                    val isOta = method is InstallMethod.DirectInstallToInactiveSlot
                    val partitionSelection = partitionsState.getOrNull(partitionSelectionIndex)
                    val flashIt = FlashIt.FlashBoot(
                        boot = if (method is InstallMethod.SelectFile) method.uri else null,
                        lkm = lkmSelection,
                        ota = isOta,
                        partition = partitionSelection
                    )
                    navigator.push(Route.Flash(flashIt))
                }
            }
        }
    }

    // 槽位选择对话框
    if (anyKernel3State.showSlotSelectionDialog && isAbDevice) {
        SlotSelectionDialogMaterial(
            show = true,
            onDismiss = { anyKernel3State.onDismissSlotDialog() },
            onSlotSelected = { slot ->
                anyKernel3State.onSlotSelected(slot)
            }
        )
    }

    // KPM补丁选择对话框
    if (anyKernel3State.showKpmPatchDialog) {
        KpmPatchSelectionDialogMaterial(
            show = true,
            currentOption = anyKernel3State.kpmPatchOption,
            onDismiss = { anyKernel3State.onDismissPatchDialog() },
            onOptionSelected = { option ->
                anyKernel3State.onOptionSelected(option)
            }
        )
    }

    val currentKmi by produceState(initialValue = "") { value = getCurrentKmi() }

    val showChooseKmiDialog = rememberSaveable { mutableStateOf(false) }
    val chooseKmiDialog = ChooseKmiDialog(showChooseKmiDialog) { kmi ->
        kmi?.let {
            lkmSelection = LkmSelection.KmiString(it)
            onInstall()
        }
    }

    val onClickNext = {
        val isLkmSelected = lkmSelection != LkmSelection.KmiNone
        val isKmiUnknown = currentKmi.isBlank()
        val isSelectFileMode = installMethod is InstallMethod.SelectFile
        if (isGKI && !isLkmSelected && (isKmiUnknown || isSelectFileMode) && installMethod !is InstallMethod.HorizonKernel) {
            // no lkm file selected and cannot get current kmi or select file mode
            showChooseKmiDialog.value = true
            chooseKmiDialog
        } else {
            onInstall()
        }
    }

    val selectLkmLauncher =
        rememberLauncherForActivityResult(contract = ActivityResultContracts.StartActivityForResult()) {
            if (it.resultCode == Activity.RESULT_OK) {
                it.data?.data?.let { uri ->
                    val isKo = isKoFile(context, uri)
                    if (isKo) {
                        lkmSelection = LkmSelection.LkmUri(uri)
                    } else {
                        lkmSelection = LkmSelection.KmiNone
                        Toast.makeText(
                            context,
                            R.string.install_only_support_ko_file,
                            Toast.LENGTH_SHORT
                        ).show()
                    }
                }
            }
        }

    val onLkmUpload = {
        selectLkmLauncher.launch(Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "application/octet-stream"
        })
    }

    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())

    Scaffold(
        topBar = {
            TopBar(
                onBack = dropUnlessResumed { navigator.pop() },
                scrollBehavior = scrollBehavior,
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .verticalScroll(rememberScrollState())
        ) {
            if (BuildConfig.INSTALLER_4K_ONLY) {
                Text(
                    text = stringResource(R.string.install_4k_only_notice),
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    color = MaterialTheme.colorScheme.error
                )
            }
            SelectInstallMethod(
                onSelected = { method ->
                    if (method is InstallMethod.HorizonKernel && method.uri != null) {
                        anyKernel3State.onHorizonKernelSelected(method)
                    } else {
                        installMethod = method
                    }
                },
                isAbDevice = isAbDevice
            )
            val isOta = installMethod is InstallMethod.DirectInstallToInactiveSlot
            val suffix = produceState(initialValue = "", isOta) {
                value = getSlotSuffix(isOta)
            }.value
            val partitions by produceState(initialValue = emptyList<String>()) {
                value = getAvailablePartitions()
            }
            val defaultPartition by produceState(initialValue = "") {
                value = getDefaultPartition()
            }
            LaunchedEffect(partitions) {
                partitionsState = partitions
            }
            val defaultIndex = remember(partitions, defaultPartition) {
                partitions.indexOf(defaultPartition).coerceAtLeast(0)
            }
            LaunchedEffect(defaultIndex, hasCustomSelected) {
                if (!hasCustomSelected) {
                    partitionSelectionIndex = defaultIndex
                }
            }
            val displayPartitions = remember(partitions, defaultPartition) {
                partitions.map { name ->
                    if (defaultPartition == name) "$name (default)" else name
                }
            }
            if (isGKI) {
                ExpressiveColumn(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    content = listOf(
                        {
                            if (partitions.isNotEmpty()) {
                                ExpressiveDropdownItem(
                                    enabled = installMethod is InstallMethod.DirectInstall || installMethod is InstallMethod.DirectInstallToInactiveSlot,
                                    items = displayPartitions,
                                    selectedIndex = partitionSelectionIndex,
                                    title = "${stringResource(R.string.install_select_partition)} (${suffix})",
                                    onItemSelected = { index ->
                                        hasCustomSelected = true
                                        partitionSelectionIndex = index
                                    },
                                    icon = Icons.Filled.Edit
                                )
                            }
                        },
                        {
                            ExpressiveListItem(
                                leadingContent = {
                                    Icon(
                                        Icons.AutoMirrored.Filled.DriveFileMove,
                                        null
                                    )
                                },
                                headlineContent = { Text(stringResource(R.string.install_upload_lkm_file)) },
                                supportingContent = {
                                    (lkmSelection as? LkmSelection.LkmUri)?.let {
                                        Text(
                                            stringResource(
                                                R.string.selected_lkm,
                                                it.uri.lastPathSegment ?: "(file)"
                                            )
                                        )
                                    }
                                },
                                trailingContent = {
                                    Icon(
                                        Icons.AutoMirrored.Filled.KeyboardArrowRight,
                                        null
                                    )
                                },
                                onClick = { onLkmUpload() }
                            )
                        }
                    )
                )
            }

            // AnyKernel3 刷写
            (installMethod as? InstallMethod.HorizonKernel)?.let { method ->
                ExpressiveColumn(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    content = listOf({
                        if (isAbDevice && method.slot != null) {
                            ExpressiveListItem(
                                onClick = { anyKernel3State.onReopenSlotDialog(method) },
                                leadingContent = {
                                    Icon(
                                        Icons.Filled.SdStorage,
                                        tint = MaterialTheme.colorScheme.primary,
                                        contentDescription = null
                                    )
                                },
                                headlineContent = {
                                    Text(
                                        stringResource(
                                            id = R.string.selected_slot,
                                            if (method.slot == "a") stringResource(id = R.string.slot_a)
                                            else stringResource(id = R.string.slot_b)
                                        )
                                    )
                                },
                                trailingContent = {
                                    Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null)
                                }
                            )
                        }
                    }, {
                        ExpressiveListItem(
                            onClick = { anyKernel3State.onReopenKpmDialog(method) },
                            leadingContent = {
                                Icon(
                                    Icons.Filled.Security,
                                    tint = when (kpmPatchOption) {
                                        KpmPatchOption.PATCH_KPM -> MaterialTheme.colorScheme.primary
                                        KpmPatchOption.UNDO_PATCH_KPM -> MaterialTheme.colorScheme.secondary
                                        KpmPatchOption.FOLLOW_KERNEL -> MaterialTheme.colorScheme.onSurfaceVariant
                                    },
                                    contentDescription = null
                                )
                            },
                            headlineContent = {
                                Text(
                                    when (kpmPatchOption) {
                                        KpmPatchOption.PATCH_KPM -> stringResource(R.string.kpm_patch_enabled)
                                        KpmPatchOption.UNDO_PATCH_KPM -> stringResource(R.string.kpm_undo_patch_enabled)
                                        KpmPatchOption.FOLLOW_KERNEL -> stringResource(R.string.kpm_follow_kernel_file)
                                    }
                                )
                            },
                            trailingContent = {
                                Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null)
                            }
                        )
                    })
                )
            }

            Button(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                enabled = installMethod != null,
                onClick = { onClickNext() }
            ) { Text(stringResource(R.string.install_next)) }
        }
    }
}

@Composable
private fun SelectInstallMethod(
    onSelected: (InstallMethod) -> Unit = {},
    isAbDevice: Boolean = false) {
    val rootAvailable = rootAvailable()
    val defaultPartitionName = produceState(initialValue = "boot") {
        value = getDefaultPartition()
    }.value
    val isGkiDevice = produceState(initialValue = false) {
        value = getKernelVersion().isGKI()
    }.value

    val selectFileTip = stringResource(
        id = R.string.select_file_tip, defaultPartitionName
    )
    val selectFileTipNoGKI = stringResource(id = R.string.select_file_tip_nogki)
    val horizonKernelSummary = stringResource(R.string.horizon_kernel_summary)
    val radioOptions =
        mutableListOf<InstallMethod>(InstallMethod.SelectFile(summary = if (isGkiDevice) selectFileTip else selectFileTipNoGKI))
    if (rootAvailable && isGkiDevice) {
        radioOptions.add(InstallMethod.DirectInstall)

        if (isAbDevice) {
            radioOptions.add(InstallMethod.DirectInstallToInactiveSlot)
        }
        radioOptions.add(InstallMethod.HorizonKernel(summary = horizonKernelSummary))
    }

    var selectedOption by remember { mutableStateOf<InstallMethod?>(null) }
    var currentSelectingMethod by remember { mutableStateOf<InstallMethod?>(null) }

    val selectImageLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        if (it.resultCode == Activity.RESULT_OK) {
            it.data?.data?.let { uri ->
                val option = when (currentSelectingMethod) {
                    is InstallMethod.SelectFile -> InstallMethod.SelectFile(uri, summary = selectFileTip)
                    is InstallMethod.HorizonKernel -> InstallMethod.HorizonKernel(uri, summary = horizonKernelSummary)
                    else -> null
                }
                option?.let { opt ->
                    selectedOption = opt
                    onSelected(opt)
                }
            }
        }
    }

    val confirmDialog = rememberConfirmDialog(
        onConfirm = {
            selectedOption = InstallMethod.DirectInstallToInactiveSlot
            onSelected(InstallMethod.DirectInstallToInactiveSlot)
        },
        onDismiss = null
    )
    val dialogTitle = stringResource(android.R.string.dialog_alert_title)
    val dialogContent = stringResource(R.string.install_inactive_slot_warning)

    val onClick = { option: InstallMethod ->
        currentSelectingMethod = option
        when (option) {
            is InstallMethod.SelectFile, is InstallMethod.HorizonKernel -> {
                selectImageLauncher.launch(Intent(Intent.ACTION_GET_CONTENT).apply {
                    type = "application/*"
                    putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/zip"))
                })
            }

            is InstallMethod.DirectInstall -> {
                selectedOption = option
                onSelected(option)
            }

            is InstallMethod.DirectInstallToInactiveSlot -> {
                confirmDialog.showConfirm(dialogTitle, dialogContent)
            }
        }
    }

    key(isAbDevice) {
        ExpressiveColumn(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
            selectedIndices = radioOptions.indexOfFirst { it.javaClass == selectedOption?.javaClass }.let { if (it >= 0) setOf(it) else emptySet() },
            content = radioOptions.map { option ->
                {
                    ExpressiveRadioItem(
                        title = stringResource(option.label),
                        summary = option.summary,
                        selected = option.javaClass == selectedOption?.javaClass,
                        onClick = { onClick(option) }
                    )
                }
            }
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    onBack: () -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    TopAppBar(
        title = { Text(stringResource(R.string.install)) },
        navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null)
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}
